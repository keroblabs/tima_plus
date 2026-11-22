/* umac SDL2 main application
 *
 * Opens an SDL2 window, allocates RAM/loads the ROM, routes
 * mouse/keyboard updates to umac, and blits framebuffer to the
 * display.
 *
 * Copyright 2024 Matt Evans
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "SDL.h"

#include "rom.h"
#include "rtc.h"
#include "umac.h"
#include "machw.h"
#include "scsi_disk.h"

#include "keymap_sdl.h"

static void     print_help(char *n)
{
        printf("Syntax: %s <options>\n"
               "\t-r <rom path>\t\tDefault 'rom.bin'\n"
               "\t-W <rom dump path>\tDump the original ROM bytes\n"
               "\t-d <floppy path>\tAttach a raw or DiskCopy 4.2 Macintosh 800K image (read-only)\n"
               "\t-w\t\t\tEnable persistent floppy writes (requires -d)\n"
               "\t-s <SCSI image path>\tAttach a raw 512-byte-sector hard disk (default R/O)\n"
               "\t-t <target ID>\t\tSCSI target ID, 0-6 (default 1)\n"
               "\t-p\t\t\tEnable persistent SCSI hard disk writes (requires -s)\n"
               "\t-P <pram path>\t\tRTC/PRAM backing file (default 'pram.nv')\n"
               "\t-i\t\t\tDisassembled instruction trace\n"
               "\t--trace-iwm\t\tTrace IWM and Sony drive events\n"
               "\t--raw-rom\t\tBoot a 128 KiB raw reset ROM and skip the Plus V3 checksum\n", n);
}

#define DISP_SCALE      2
#define UMAC_MOUSE_FREE_TITLE "umac (click to capture mouse)"

static uint32_t framebuffer[MAC_SCREEN_WIDTH*MAC_SCREEN_HEIGHT];

static bool     set_mouse_capture(SDL_Window *window, SDL_bool capture)
{
        if (SDL_SetRelativeMouseMode(capture) != 0) {
                fprintf(stderr, "Cannot %s the mouse: %s\n",
                        capture ? "capture" : "release", SDL_GetError());
                return false;
        }
        SDL_SetWindowTitle(window, capture ? "umac (F12 releases mouse)" : UMAC_MOUSE_FREE_TITLE);
        return true;
}

static bool     same_file(int fd, const char *path)
{
        struct stat opened, other;
        return path && fstat(fd, &opened) == 0 && stat(path, &other) == 0 &&
               opened.st_dev == other.st_dev && opened.st_ino == other.st_ino;
}

/* Resolve existing files normally.  For a new output, resolve its parent
 * and append the basename so aliases such as ./dir/../file compare equal. */
static char     *canonical_path_for_create(const char *path)
{
        if (!path || !*path) {
                errno = EINVAL;
                return NULL;
        }

        char *resolved = realpath(path, NULL);
        if (resolved)
                return resolved;
        if (errno != ENOENT)
                return NULL;

        struct stat st;
        if (lstat(path, &st) == 0) {
                errno = ELOOP;       /* Includes dangling symbolic links. */
                return NULL;
        }
        if (errno != ENOENT)
                return NULL;

        char *copy = strdup(path);
        if (!copy)
                return NULL;
        char *slash = strrchr(copy, '/');
        char *base = copy;
        const char *parent = ".";
        if (slash) {
                base = slash + 1;
                if (slash == copy)
                        parent = "/";
                else {
                        *slash = '\0';
                        parent = copy;
                }
        }
        if (!*base || !strcmp(base, ".") || !strcmp(base, "..")) {
                free(copy);
                errno = EINVAL;
                return NULL;
        }

        char *real_parent = realpath(parent, NULL);
        if (!real_parent) {
                int saved_errno = errno;
                free(copy);
                errno = saved_errno;
                return NULL;
        }
        size_t needed = strlen(real_parent) + strlen(base) + 2;
        resolved = malloc(needed);
        if (resolved) {
                snprintf(resolved, needed, "%s%s%s", real_parent,
                         !strcmp(real_parent, "/") ? "" : "/", base);
        } else {
                errno = ENOMEM;
        }
        int saved_errno = errno;
        free(real_parent);
        free(copy);
        errno = saved_errno;
        return resolved;
}

static int      paths_alias(const char *a, const char *b, bool *alias)
{
        char *canonical_a = canonical_path_for_create(a);
        char *canonical_b = canonical_path_for_create(b);
        if (!canonical_a || !canonical_b) {
                int saved_errno = errno;
                free(canonical_a);
                free(canonical_b);
                errno = saved_errno;
                return -1;
        }
        *alias = !strcmp(canonical_a, canonical_b);
        free(canonical_a);
        free(canonical_b);
        if (*alias)
                return 0;

        struct stat stat_a, stat_b;
        if (stat(a, &stat_a) == 0 && stat(b, &stat_b) == 0) {
                *alias = stat_a.st_dev == stat_b.st_dev &&
                         stat_a.st_ino == stat_b.st_ino;
                return 0;
        }
        if ((errno == ENOENT) || (errno == ENOTDIR)) {
                *alias = false;
                return 0;
        }
        return -1;
}

static int      find_path_alias(const char *path, const char *const *protected_paths,
                                size_t protected_count, const char **conflicting_path)
{
        for (size_t i = 0; i < protected_count; i++) {
                if (!protected_paths[i])
                        continue;
                bool alias;
                if (paths_alias(path, protected_paths[i], &alias) != 0)
                        return -1;
                if (alias) {
                        *conflicting_path = protected_paths[i];
                        return 1;
                }
        }
        return 0;
}

/* Blit a 1bpp FB to a 32BPP RGBA output.  SDL2 doesn't appear to support
 * bitmap/1bpp textures, so expand.
 */
static void     copy_fb(uint32_t *fb_out, uint8_t *fb_in)
{
        // Output L-R, big-endian shorts, with bits in MSB-LSB order:
        for (int y = 0; y < MAC_SCREEN_HEIGHT; y++) {
                for (int x = 0; x < MAC_SCREEN_WIDTH; x += 16) {
                        uint8_t plo = fb_in[x/8 + (y * MAC_SCREEN_WIDTH/8) + 0];
                        uint8_t phi = fb_in[x/8 + (y * MAC_SCREEN_WIDTH/8) + 1];
                        for (int i = 0; i < 8; i++) {
                                *fb_out++ = (plo & (0x80 >> i)) ? 0 : 0xffffffff;
                        }
                        for (int i = 0; i < 8; i++) {
                                *fb_out++ = (phi & (0x80 >> i)) ? 0 : 0xffffffff;
                        }
                }
        }
}

/**********************************************************************/

int     main(int argc, char *argv[])
{
        void *ram_base = MAP_FAILED;
        void *rom_base;
        struct disk_img *floppy = NULL;
        const char *rom_filename = "rom.bin";
        const char *rom_dump_filename = NULL;
        const char *ram_filename = "ram.bin";
        const char *disc_filename = NULL;
        int ofd;
        int ch;
        int opt_disassemble = 0;
        bool opt_iwm_trace = false;
        bool raw_rom = false;
        bool floppy_write = false;
        const char *scsi_filename = NULL;
        int scsi_id = -1;
        bool scsi_write = false;
        const char *pram_filename = "pram.nv";
        char *pram_path = NULL;
        int pram_lock_fd = -1;

        ////////////////////////////////////////////////////////////////////////
        // Args

        const struct option long_options[] = {
                {"trace-iwm", no_argument, NULL, 'I'},
                {"raw-rom", no_argument, NULL, 'R'},
                {0}
        };
        while ((ch = getopt_long(argc, argv, "r:d:W:s:t:P:ihwp", long_options, NULL)) != -1) {
                switch (ch) {
                case 'r':
                        rom_filename = optarg;
                        break;

                case 'i':
                        opt_disassemble = 1;
                        break;

                case 'd':
                        disc_filename = optarg;
                        break;

                case 'w':
                        floppy_write = true;
                        break;

                case 'I':
                        opt_iwm_trace = true;
                        break;

                case 'R':
                        raw_rom = true;
                        break;

                case 'W':
                        rom_dump_filename = optarg;
                        break;

                case 's':
                        scsi_filename = optarg;
                        break;

                case 't':
                        if (optarg[0] < '0' || optarg[0] > '6' || optarg[1] != '\0') {
                                fprintf(stderr, "SCSI target ID must be between 0 and 6\n");
                                return 1;
                        }
                        scsi_id = optarg[0] - '0';
                        break;

                case 'p':
                        scsi_write = true;
                        break;

                case 'P':
                        pram_filename = optarg;
                        break;

                case 'h':
                default:
                        print_help(argv[0]);
                        return 1;
                }
        }
        if (floppy_write && !disc_filename) {
                fprintf(stderr, "-w requires a floppy image (-d)\n");
                return 1;
        }
        if (!scsi_filename && (scsi_id != -1 || scsi_write)) {
                fprintf(stderr, "-t and -p require a SCSI image (-s)\n");
                return 1;
        }
        if (scsi_id == -1)
                scsi_id = 1;

        /* RAM and ROM dumps are writable outputs. Reject aliases of the
         * read-only inputs before creating or truncating either output. */
        const char *inputs[] = {rom_filename, disc_filename, scsi_filename, rom_dump_filename};
        size_t count = sizeof(inputs) / sizeof(inputs[0]);
        const char *conflicting_path = NULL;
        int conflict = find_path_alias(ram_filename, inputs, count, &conflicting_path);
        const char *output = ram_filename;
        if (!conflict && rom_dump_filename) {
                output = rom_dump_filename;
                conflict = find_path_alias(output, inputs, count - 1, &conflicting_path);
        }
        if (conflict != 0) {
                if (conflict > 0)
                        fprintf(stderr, "Output '%s' conflicts with media/output path '%s'\n",
                                output, conflicting_path);
                else
                        fprintf(stderr, "Cannot validate output '%s': %s\n", output, strerror(errno));
                return 1;
        }

        /* Resolve and validate the PRAM destination before any media or
         * output file is opened.  This catches direct, symlink and hard-link
         * aliases of the ROM, RAM, floppy, SCSI image or ROM dump. */
        pram_path = canonical_path_for_create(pram_filename);
        if (!pram_path) {
                fprintf(stderr, "Invalid RTC/PRAM path '%s': %s\n",
                        pram_filename, strerror(errno));
                return 1;
        }
        const char *protected_paths[] = {
                rom_filename, ram_filename, rom_dump_filename,
                disc_filename, scsi_filename
        };
        conflict = find_path_alias(pram_path, protected_paths,
                                   sizeof(protected_paths) / sizeof(protected_paths[0]),
                                   &conflicting_path);
        if (conflict != 0) {
                if (conflict > 0)
                        fprintf(stderr, "RTC/PRAM path '%s' conflicts with media/output path '%s'\n",
                                pram_filename, conflicting_path);
                else
                        fprintf(stderr, "Cannot validate RTC/PRAM path '%s': %s\n",
                                pram_filename, strerror(errno));
                free(pram_path);
                return 1;
        }

        size_t lock_path_len = strlen(pram_path) + sizeof(".lock");
        char *pram_lock_path = malloc(lock_path_len);
        if (!pram_lock_path) {
                fprintf(stderr, "Out of memory validating RTC/PRAM path\n");
                free(pram_path);
                return 1;
        }
        snprintf(pram_lock_path, lock_path_len, "%s.lock", pram_path);
        conflict = find_path_alias(pram_lock_path, protected_paths,
                                   sizeof(protected_paths) / sizeof(protected_paths[0]),
                                   &conflicting_path);
        if (conflict != 0) {
                if (conflict > 0)
                        fprintf(stderr, "RTC/PRAM lock path conflicts with media/output path '%s'\n",
                                conflicting_path);
                else
                        fprintf(stderr, "Cannot validate RTC/PRAM lock path: %s\n",
                                strerror(errno));
                free(pram_lock_path);
                free(pram_path);
                return 1;
        }

        if (floppy_write) {
                const char *other_media[] = {rom_filename, scsi_filename};
                conflict = find_path_alias(disc_filename, other_media, 2, &conflicting_path);
                char *floppy_lock = NULL;
                if (!conflict) {
                        const char *lock_protected[] = {rom_filename, ram_filename, rom_dump_filename,
                                                         disc_filename, scsi_filename, pram_path, pram_lock_path};
                        floppy_lock = disk_img_lock_path(disc_filename);
                        conflict = floppy_lock ? find_path_alias(floppy_lock, lock_protected,
                                                                 sizeof(lock_protected) / sizeof(lock_protected[0]),
                                                                 &conflicting_path) : -1;
                }
                if (conflict != 0) {
                        const char *path = floppy_lock ? floppy_lock : disc_filename;
                        if (conflict > 0)
                                fprintf(stderr, "Writable floppy path '%s' conflicts with media/output path '%s'\n",
                                        path, conflicting_path);
                        else
                                fprintf(stderr, "Cannot validate writable floppy path '%s': %s\n", path, strerror(errno));
                        free(floppy_lock);
                        free(pram_lock_path);
                        free(pram_path);
                        return 1;
                }
                free(floppy_lock);
        }
        free(pram_lock_path);

        int pram_state = rtc_pram_check(pram_path);
        if (pram_state < 0) {
                fprintf(stderr, "RTC/PRAM backing file '%s' is invalid or inaccessible: %s\n",
                        pram_filename, strerror(errno));
                free(pram_path);
                return 1;
        }

        ////////////////////////////////////////////////////////////////////////
        // Load memories/discs

        printf("Opening ROM '%s'\n", rom_filename);
        ofd = open(rom_filename, O_RDONLY | O_NONBLOCK);
        if (ofd < 0) {
                perror("ROM");
                free(pram_path);
                return 1;
        }

        struct stat sb;
        if (fstat(ofd, &sb) || !S_ISREG(sb.st_mode) || sb.st_size != ROM_SIZE) {
                fprintf(stderr, "ROM '%s': expected a regular 128 KiB ROM file\n", rom_filename);
                close(ofd);
                free(pram_path);
                return 1;
        }
        rom_base = mmap(NULL, ROM_SIZE, PROT_READ, MAP_PRIVATE, ofd, 0);
        close(ofd);
        if (rom_base == MAP_FAILED) {
                perror("ROM mmap");
                free(pram_path);
                return 1;
        }
        if (!raw_rom && !rom_is_plus_v3(rom_base)) {
                fprintf(stderr, "ROM '%s': expected Macintosh Plus V3 with checksum %08" PRIx32 "\n",
                        rom_filename, ROM_PLUS_V3_CHECKSUM);
                munmap(rom_base, ROM_SIZE);
                free(pram_path);
                return 1;
        }
        if (raw_rom)
                puts("Raw reset ROM (Macintosh Plus V3 check disabled)");
        else
                printf("Macintosh Plus V3 ROM, checksum %08" PRIx32 "\n", ROM_PLUS_V3_CHECKSUM);

        floppy = floppy_write ? disk_img_open_writable(disc_filename) : disk_img_open(disc_filename);
        if (!floppy) {
                const char *message;
                if (errno == EINVAL)
                        message = "expected a raw or DiskCopy 4.2 Macintosh 800K image";
                else if (errno == EBADMSG)
                        message = "DiskCopy 4.2 checksum mismatch";
                else
                        message = strerror(errno);
                fprintf(stderr, "Floppy '%s': %s\n", disc_filename ? disc_filename : "(empty drive)", message);
                munmap(rom_base, ROM_SIZE);
                free(pram_path);
                return 1;
        }
        if (disc_filename)
                printf("Floppy: '%s' (800K, %s)\n", disc_filename, floppy_write ? "writable" : "write protected");

        struct scsi_image scsi_image;
        struct scsi_disk scsi_disk;
        int result = 1;
        if (scsi_filename) {
                if (scsi_image_open(&scsi_image, scsi_filename, scsi_write)) {
                        fprintf(stderr, "SCSI image '%s': %s\n", scsi_filename, strerror(errno));
                        goto close_media;
                }
                if (same_file(scsi_image.fd, ram_filename) || same_file(scsi_image.fd, rom_filename) ||
                    same_file(scsi_image.fd, rom_dump_filename) || same_file(scsi_image.fd, disc_filename)) {
                        fprintf(stderr, "Use a separate file for the SCSI image (not RAM, ROM or floppy)\n");
                        goto close_scsi;
                }
                scsi_disk_init(&scsi_disk, &scsi_image.media);
                printf("SCSI ID %d: '%s' (%s)\n", scsi_id, scsi_filename,
                       scsi_write ? "writable" : "read-only");
        }

        /* A sidecar flock stays attached to one stable inode while the
         * PRAM data file is atomically replaced on save. */
        pram_lock_fd = rtc_pram_lock(pram_path);
        if (pram_lock_fd < 0) {
                fprintf(stderr, "RTC/PRAM backing file '%s' is already in use or cannot be locked: %s\n",
                        pram_filename, strerror(errno));
                goto close_scsi;
        }
        pram_state = rtc_pram_check(pram_path);
        if (pram_state < 0) {
                fprintf(stderr, "RTC/PRAM backing file '%s' changed or became invalid: %s\n",
                        pram_filename, strerror(errno));
                goto close_scsi;
        }

        if (rom_dump_filename) {
                int rfd = open(rom_dump_filename, O_CREAT | O_TRUNC | O_WRONLY, 0644);
                if (rfd < 0) {
                        perror("ROM dump");
                        goto close_scsi;
                }
                ssize_t written = write(rfd, rom_base, ROM_SIZE);
                close(rfd);
                if (written != ROM_SIZE) {
                        fprintf(stderr, "Could not write the complete ROM to '%s'\n", rom_dump_filename);
                        goto close_scsi;
                }
                printf("Dumped ROM to %s\n", rom_dump_filename);
        }

        /* Set up RAM, shared file map: */
        ofd = open(ram_filename, O_CREAT | O_TRUNC | O_RDWR, 0644);
        if (ofd < 0) {
                perror("RAM");
                goto close_scsi;
        }
        if (ftruncate(ofd, RAM_SIZE)) {
                perror("RAM ftruncate");
                close(ofd);
                goto close_scsi;
        }
        ram_base = mmap(0, RAM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, ofd, 0);
        close(ofd);
        if (ram_base == MAP_FAILED) {
                perror("RAM mmap");
                goto close_scsi;
        }
        printf("RAM mapped at %p\n", (void *)ram_base);

        ////////////////////////////////////////////////////////////////////////
        // SDL/UI init

        SDL_Renderer *renderer;
        SDL_Texture *texture;

        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
                fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
                goto close_scsi;
        }
        SDL_Window *window = SDL_CreateWindow(UMAC_MOUSE_FREE_TITLE,
                                              SDL_WINDOWPOS_UNDEFINED,
                                              SDL_WINDOWPOS_UNDEFINED,
                                              MAC_SCREEN_WIDTH * DISP_SCALE,
                                              MAC_SCREEN_HEIGHT * DISP_SCALE,
                                              0);
        if (!window) {
                fprintf(stderr, "SDL window creation failed: %s\n", SDL_GetError());
                goto close_sdl;
        }
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

        renderer = SDL_CreateRenderer(window, -1, 0);
        if (!renderer) {
                fprintf(stderr, "SDL renderer creation failed: %s\n", SDL_GetError());
                goto close_sdl;
        }

        texture = SDL_CreateTexture(renderer,
                                    SDL_PIXELFORMAT_RGBA32,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    MAC_SCREEN_WIDTH,
                                    MAC_SCREEN_HEIGHT);
        if (!texture) {
                fprintf(stderr, "SDL texture creation failed: %s\n", SDL_GetError());
                goto close_sdl;
        }

        ////////////////////////////////////////////////////////////////////////
        // Emulator init

        umac_init(ram_base, rom_base, floppy);
#ifdef UMAC_DEVELOPMENT_DUART
        umac_debug_uart(stdout);
#endif
        umac_opt_disassemble(opt_disassemble);
        if (opt_iwm_trace)
                umac_iwm_trace(stderr);
        pram_state = umac_rtc_load(pram_path);
        if (pram_state < 0) {
                fprintf(stderr, "Cannot load RTC/PRAM backing file '%s': %s\n",
                        pram_filename, strerror(errno));
                goto close_sdl;
        }
        if (pram_state == RTC_PRAM_MISSING)
                printf("RTC/PRAM backing file '%s' will be created on clean exit\n",
                       pram_filename);
        if (scsi_filename) {
                struct scsi_target target = scsi_disk_target(&scsi_disk, scsi_id);
                if (!ncr5380_attach(&target)) {
                        fprintf(stderr, "Cannot attach SCSI target %d: invalid descriptor\n", scsi_id);
                        goto close_sdl;
                }
        }

        ////////////////////////////////////////////////////////////////////////
        // Main loop

        int done = 0;
        bool cpu_done = false;
        int mouse_button = 0;
        const uint64_t host_hz = SDL_GetPerformanceFrequency();
        const uint64_t present_interval = host_hz / 60;
        const uint64_t vbl_interval = host_hz * MAC_FRAME_TICKS / MAC_C8M_HZ;
        uint64_t next_present = SDL_GetPerformanceCounter();
        uint64_t next_vsync = next_present + vbl_interval;
        uint64_t next_1hz = next_present + host_hz;
        do {
                SDL_Event event;
                bool quit_requested = false;
                int mousex = 0;
                int mousey = 0;

                if (SDL_PollEvent(&event)) {
                        switch (event.type) {
                        case SDL_QUIT:
                                quit_requested = true;
                                break;

                        case SDL_WINDOWEVENT:
                                if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                                        set_mouse_capture(window, SDL_FALSE);
                                        mouse_button = 0;
                                }
                                break;

                        case SDL_KEYDOWN:
                        case SDL_KEYUP: {
                                if (event.key.keysym.scancode == SDL_SCANCODE_F12) {
                                        if (event.type == SDL_KEYDOWN) {
                                                set_mouse_capture(window, SDL_FALSE);
                                                mouse_button = 0;
                                        }
                                        break;
                                }
                                int c = SDLScan2MacKeyCode(event.key.keysym.scancode);
                                c = (c << 1) | 1;
                                printf("Key 0x%x -> 0x%x\n", event.key.keysym.scancode, c);
                                if (c != MKC_None)
                                        umac_kbd_event(c, (event.type == SDL_KEYDOWN));
                        } break;

                        case SDL_MOUSEMOTION:
                                mousex = event.motion.xrel;
                                mousey = -event.motion.yrel;
                                break;

                        case SDL_MOUSEBUTTONDOWN:
                                if (event.button.button == SDL_BUTTON_LEFT &&
                                    !SDL_GetRelativeMouseMode() && set_mouse_capture(window, SDL_TRUE))
                                        break;
                                mouse_button = 1;
                                break;

                        case SDL_MOUSEBUTTONUP:
                                mouse_button = 0;
                                break;
                        }
                }

                if (!cpu_done && !quit_requested) {
                        umac_mouse(mousex, mousey, mouse_button);
                        cpu_done = umac_loop() != 0;
                        quit_requested = cpu_done;
                }
                if (quit_requested) {
                        disk_img_abort_write(floppy);
                        if (disk_img_checkpoint(floppy) == 0) {
                                done = 1;
                        } else {
                                fputs("Floppy save failed. The session remains open for a retry.\n", stderr);
                                SDL_SetWindowTitle(window, "umac (floppy save failed)");
                        }
                }

                uint64_t host_now = SDL_GetPerformanceCounter();
                /* The desktop's key repeat and double-click timing follow VBL. */
                if (host_now >= next_vsync) {
                        umac_vsync_event();
                        next_vsync = host_now + vbl_interval;
                }

                if (host_now >= next_present) {
                        next_present = host_now + present_interval;
                        copy_fb(framebuffer, ram_get_base() + umac_get_fb_offset());
                        SDL_UpdateTexture(texture, NULL, framebuffer,
                                          MAC_SCREEN_WIDTH * sizeof (Uint32));
                        /* Scales texture up to window size */
                        SDL_RenderCopy(renderer, texture, NULL, NULL);
                        SDL_RenderPresent(renderer);
                }
                /* The ROM advances its clock by one second for each CA1 event. */
                if (host_now >= next_1hz) {
                        umac_1hz_event();
                        next_1hz += host_hz;
                }
        } while (!done);

        if (umac_rtc_save(pram_path)) {
                fprintf(stderr, "Cannot save RTC/PRAM backing file '%s': %s\n",
                        pram_filename, strerror(errno));
                result = 1;
        } else {
                result = 0;
        }

close_sdl:
        SDL_Quit();
close_scsi:
        if (scsi_filename) {
                ncr5380_attach(NULL);
                if (scsi_image_close(&scsi_image)) {
                        perror("Closing SCSI image");
                        result = 1;
                }
        }
        if (pram_lock_fd >= 0)
                close(pram_lock_fd);
close_media:
        if (ram_base != MAP_FAILED)
                munmap(ram_base, RAM_SIZE);
        if (disk_img_close(floppy) < 0)
                result = 1;
        munmap(rom_base, ROM_SIZE);
        free(pram_path);
        return result;
}
