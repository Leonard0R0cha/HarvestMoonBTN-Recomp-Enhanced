/* parallel-psx's HD texture folder scan: no folder here (HD textures are off). Written for this port. */
#pragma once
typedef struct RDIR RDIR;
static inline RDIR *retro_opendir(const char *path) { (void)path; return 0; }
static inline int retro_readdir(RDIR *d) { (void)d; return 0; }
static inline const char *retro_dirent_get_name(RDIR *d) { (void)d; return ""; }
static inline void retro_closedir(RDIR *d) { (void)d; }
