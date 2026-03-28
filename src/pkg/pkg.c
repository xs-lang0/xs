#include "pkg/pkg.h"
#include "core/xs_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

static int write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "xs new: cannot write '%s'\n", path);
        return 1;
    }
    fwrite(content, 1, strlen(content), f);
    fclose(f);
    return 0;
}

static int read_pkg_version(const char *toml_path, char *version_out, size_t vlen) {
    FILE *f = fopen(toml_path, "r");
    if (!f) return -1;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *p = strstr(line, "version");
        if (!p) continue;
        p = strchr(p, '=');
        if (!p) continue;
        p++;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '"') {
            p++;
            char *end = strchr(p, '"');
            if (end) {
                size_t len = (size_t)(end - p);
                if (len >= vlen) len = vlen - 1;
                memcpy(version_out, p, len);
                version_out[len] = '\0';
                fclose(f);
                return 0;
            }
        }
    }
    fclose(f);
    return -1;
}

static int read_pkg_source(const char *toml_path, char *source_out, size_t slen) {
    FILE *f = fopen(toml_path, "r");
    if (!f) return -1;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *p = strstr(line, "source");
        if (!p) continue;
        p = strchr(p, '=');
        if (!p) continue;
        p++;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '"') {
            p++;
            char *end = strchr(p, '"');
            if (end) {
                size_t len = (size_t)(end - p);
                if (len >= slen) len = slen - 1;
                memcpy(source_out, p, len);
                source_out[len] = '\0';
                fclose(f);
                return 0;
            }
        }
    }
    fclose(f);
    return -1;
}

static int is_directory(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

static int file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0);
}

static int count_pkg_files(const char *pkg_dir) {
    DIR *d = opendir(pkg_dir);
    if (!d) return 0;
    struct dirent *ent;
    int count = 0;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        count++;
    }
    closedir(d);
    return count;
}

static int copy_directory(const char *src, const char *dst) {
    mkdir(dst, 0755);
    DIR *d = opendir(src);
    if (!d) {
        fprintf(stderr, "xs install: cannot open source directory '%s'\n", src);
        return 1;
    }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char srcpath[1024], dstpath[1024];
        snprintf(srcpath, sizeof(srcpath), "%s/%s", src, ent->d_name);
        snprintf(dstpath, sizeof(dstpath), "%s/%s", dst, ent->d_name);
        if (is_directory(srcpath)) {
            if (copy_directory(srcpath, dstpath) != 0) {
                closedir(d);
                return 1;
            }
        } else {
            FILE *in = fopen(srcpath, "rb");
            if (!in) { closedir(d); return 1; }
            FILE *out = fopen(dstpath, "wb");
            if (!out) { fclose(in); closedir(d); return 1; }
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
                fwrite(buf, 1, n, out);
            }
            fclose(in);
            fclose(out);
        }
    }
    closedir(d);
    return 0;
}

static int remove_recursive(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;

    if (!S_ISDIR(st.st_mode)) return remove(path);

    DIR *d = opendir(path);
    if (!d) return -1;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char child[2048];
        snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
        remove_recursive(child);
    }
    closedir(d);
    return rmdir(path);
}

static const char *basename_of(const char *path) {
    const char *slash = strrchr(path, '/');
    if (slash) return slash + 1;
    return path;
}

/* strip .git suffix from a name, returns static buffer */
static const char *strip_git_suffix(const char *name) {
    static char buf[512];
    size_t len = strlen(name);
    if (len >= 4 && strcmp(name + len - 4, ".git") == 0) {
        if (len - 4 >= sizeof(buf)) len = sizeof(buf) - 1 + 4;
        memcpy(buf, name, len - 4);
        buf[len - 4] = '\0';
        return buf;
    }
    return name;
}


int pkg_new(const char *name) {
    if (!name || !name[0]) {
        fprintf(stderr, "xs new: missing project name\n");
        return 1;
    }

    if (mkdir(name, 0755) != 0) {
        fprintf(stderr, "xs new: cannot create directory '%s'\n", name);
        return 1;
    }

    char pathbuf[1024];
    snprintf(pathbuf, sizeof pathbuf, "%s/src", name);
    if (mkdir(pathbuf, 0755) != 0) {
        fprintf(stderr, "xs new: cannot create directory '%s'\n", pathbuf);
        return 1;
    }

    snprintf(pathbuf, sizeof pathbuf, "%s/xs.toml", name);
    char toml[1024];
    snprintf(toml, sizeof toml,
        "[package]\n"
        "name = \"%s\"\n"
        "version = \"0.1.0\"\n"
        "xs_version = \">=1.0\"\n"
        "\n"
        "[dependencies]\n"
        "\n"
        "[build]\n"
        "entry = \"src/main.xs\"\n",
        name);
    if (write_file(pathbuf, toml)) return 1;

    snprintf(pathbuf, sizeof pathbuf, "%s/src/main.xs", name);
    char mainxs[512];
    snprintf(mainxs, sizeof mainxs,
        "fn main() {\n"
        "    println(\"Hello from %s!\")\n"
        "}\n",
        name);
    if (write_file(pathbuf, mainxs)) return 1;

    snprintf(pathbuf, sizeof pathbuf, "%s/.gitignore", name);
    if (write_file(pathbuf,
        "xs_modules/\n"
        "*.xsc\n"
        ".xs_cache/\n")) return 1;

    printf("Created project '%s'\n", name);
    printf("  %s/xs.toml\n", name);
    printf("  %s/src/main.xs\n", name);
    printf("  %s/.gitignore\n", name);
    return 0;
}

// xs install

int pkg_install(const char *package_name) {
    if (!package_name) {
        FILE *f = fopen("xs.toml", "r");
        if (!f) {
            fprintf(stderr, "xs install: no xs.toml found in current directory\n");
            return 1;
        }
        char line[1024];
        int in_deps = 0;
        int count = 0;
        while (fgets(line, sizeof(line), f)) {
            size_t len = strlen(line);
            if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
            if (strcmp(line, "[dependencies]") == 0) { in_deps = 1; continue; }
            if (in_deps && line[0] == '[') break; /* next section */
            if (in_deps && len > 0 && line[0] != '#') {
                char *eq = strchr(line, '=');
                if (eq) {
                    *eq = '\0';
                    char *end = eq - 1;
                    while (end >= line && (*end == ' ' || *end == '\t')) *end-- = '\0';
                    char *val = eq + 1;
                    while (*val == ' ' || *val == '\t') val++;
                    size_t vlen = strlen(val);
                    if (vlen >= 2 && val[0] == '"' && val[vlen - 1] == '"') {
                        val[vlen - 1] = '\0';
                        val++;
                    }
                    if (strlen(line) > 0) {
                        mkdir("xs_modules", 0755);
                        char dirpath[2048];
                        snprintf(dirpath, sizeof(dirpath), "xs_modules/%s", line);
                        if (strncmp(val, "file://", 7) == 0) {
                            const char *src = val + 7;
                            if (is_directory(src)) {
                                copy_directory(src, dirpath);
                                printf("  installed %s from %s\n", line, src);
                            } else {
                                fprintf(stderr, "  warning: source '%s' not found for %s\n", src, line);
                                mkdir(dirpath, 0755);
                            }
                        } else if (strstr(val, ".git") || strncmp(val, "git://", 6) == 0 ||
                                   strncmp(val, "https://", 8) == 0 || strncmp(val, "http://", 7) == 0) {
                            char cmd[4096];
                            snprintf(cmd, sizeof(cmd), "git clone --depth 1 %s %s 2>&1", val, dirpath);
                            int ret = system(cmd);
                            if (ret != 0) {
                                fprintf(stderr, "  warning: git clone failed for %s\n", line);
                                mkdir(dirpath, 0755);
                            } else {
                                printf("  installed %s from %s\n", line, val);
                            }
                        } else {
                            mkdir(dirpath, 0755);
                            /* Write xs.toml with version info */
                            char tomlpath[4096];
                            snprintf(tomlpath, sizeof(tomlpath), "%s/xs.toml", dirpath);
                            FILE *tf = fopen(tomlpath, "w");
                            if (tf) {
                                fprintf(tf,
                                    "[package]\nname = \"%s\"\nversion = \"%s\"\n",
                                    line, val);
                                fclose(tf);
                            }
                            printf("  installed %s@%s\n", line, val);
                        }
                        count++;
                    }
                }
            }
        }
        fclose(f);
        printf("installed %d dependencies\n", count);
        return 0;
    }

    mkdir("xs_modules", 0755);

    const char *pkg_name = package_name;
    const char *source = package_name;
    int is_local = 0;
    int is_git = 0;

    if (strncmp(package_name, "file://", 7) == 0) {
        is_local = 1;
        source = package_name + 7;
        pkg_name = basename_of(source);
    } else if (is_directory(package_name)) {
        is_local = 1;
        source = package_name;
        pkg_name = basename_of(source);
    } else if (strstr(package_name, ".git") || strncmp(package_name, "git://", 6) == 0 ||
               strncmp(package_name, "https://", 8) == 0 || strncmp(package_name, "http://", 7) == 0) {
        is_git = 1;
        pkg_name = strip_git_suffix(basename_of(package_name));
    }

    char dirpath[1024];
    snprintf(dirpath, sizeof(dirpath), "xs_modules/%s", pkg_name);

    if (is_local) {
        if (!is_directory(source)) {
            fprintf(stderr, "xs install: source path '%s' not found\n", source);
            return 1;
        }
        if (copy_directory(source, dirpath) != 0) {
            fprintf(stderr, "xs install: failed to copy from '%s'\n", source);
            return 1;
        }
        char tomlpath[2048];
        snprintf(tomlpath, sizeof(tomlpath), "%s/xs.toml", dirpath);
        if (!file_exists(tomlpath)) {
            FILE *f = fopen(tomlpath, "w");
            if (f) {
                fprintf(f,
                    "[package]\n"
                    "name = \"%s\"\n"
                    "version = \"0.1.0\"\n"
                    "source = \"file://%s\"\n",
                    pkg_name, source);
                fclose(f);
            }
        }
        printf("installed %s from %s\n", pkg_name, source);
    } else if (is_git) {
        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "git clone --depth 1 %s %s 2>&1", package_name, dirpath);
        int ret = system(cmd);
        if (ret != 0) {
            fprintf(stderr, "xs install: git clone failed for '%s'\n", package_name);
            return 1;
        }
        char tomlpath[2048];
        snprintf(tomlpath, sizeof(tomlpath), "%s/xs.toml", dirpath);
        if (!file_exists(tomlpath)) {
            FILE *f = fopen(tomlpath, "w");
            if (f) {
                fprintf(f,
                    "[package]\n"
                    "name = \"%s\"\n"
                    "version = \"0.1.0\"\n"
                    "source = \"%s\"\n",
                    pkg_name, package_name);
                fclose(f);
            }
        }
        printf("installed %s from %s\n", pkg_name, package_name);
    } else {
        if (mkdir(dirpath, 0755) != 0 && errno != EEXIST) {
            fprintf(stderr, "xs install: cannot create '%s'\n", dirpath);
            return 1;
        }
        char srcdir[2048];
        snprintf(srcdir, sizeof(srcdir), "%s/src", dirpath);
        mkdir(srcdir, 0755);

        char tomlpath[2048];
        snprintf(tomlpath, sizeof(tomlpath), "%s/xs.toml", dirpath);
        FILE *f = fopen(tomlpath, "w");
        if (f) {
            fprintf(f,
                "[package]\n"
                "name = \"%s\"\n"
                "version = \"0.1.0\"\n"
                "source = \"registry\"\n"
                "\n"
                "[dependencies]\n",
                package_name);
            fclose(f);
        }
        char mainpath[2048];
        snprintf(mainpath, sizeof(mainpath), "%s/src/lib.xs", dirpath);
        if (!file_exists(mainpath)) {
            FILE *mf = fopen(mainpath, "w");
            if (mf) {
                fprintf(mf, "-- %s package stub\n", package_name);
                fclose(mf);
            }
        }
        printf("installed %s\n", package_name);
    }
    return 0;
}

int pkg_remove(const char *package_name) {
    if (!package_name) {
        fprintf(stderr, "xs remove: missing package name\n");
        return 1;
    }
    char dirpath[1024];
    snprintf(dirpath, sizeof(dirpath), "xs_modules/%s", package_name);

    if (!is_directory(dirpath)) {
        fprintf(stderr, "xs remove: package '%s' not installed\n", package_name);
        return 1;
    }
    if (remove_recursive(dirpath) != 0) {
        fprintf(stderr, "xs remove: failed to fully remove '%s'\n", package_name);
        return 1;
    }
    printf("removed %s\n", package_name);
    return 0;
}

/* xs update */
int pkg_update(const char *package_name) {
    DIR *d = opendir("xs_modules");
    if (!d) {
        printf("no xs_modules/ directory found: nothing to update\n");
        return 0;
    }

    struct dirent *ent;
    int checked = 0;
    int updated = 0;

    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "xs_modules/%s", ent->d_name);
        if (!is_directory(fullpath)) continue;

        if (package_name && strcmp(ent->d_name, package_name) != 0) continue;

        char tomlpath[2048];
        snprintf(tomlpath, sizeof(tomlpath), "%s/xs.toml", fullpath);

        char version[256] = "unknown";
        char source[1024] = "";
        read_pkg_version(tomlpath, version, sizeof(version));
        read_pkg_source(tomlpath, source, sizeof(source));

        int file_count = count_pkg_files(fullpath);
        int needs_update = 0;

        if (!file_exists(tomlpath)) {
            printf("  %s: missing xs.toml: needs reinstall\n", ent->d_name);
            needs_update = 1;
        } else if (file_count <= 1) {
            printf("  %s@%s: incomplete package (only %d file(s)) -- needs reinstall\n",
                   ent->d_name, version, file_count);
            needs_update = 1;
        }

        if (needs_update) {
            if (source[0] && strcmp(source, "registry") != 0) {
                printf("  %s: re-installing from %s\n", ent->d_name, source);
                pkg_remove(ent->d_name);
                if (strncmp(source, "file://", 7) == 0) {
                    pkg_install(source);
                } else if (strstr(source, ".git") || strncmp(source, "https://", 8) == 0 ||
                           strncmp(source, "http://", 7) == 0 || strncmp(source, "git://", 6) == 0) {
                    pkg_install(source);
                } else {
                    pkg_install(ent->d_name);
                }
                updated++;
            } else {
                printf("  %s@%s: no known source: reinstall manually\n",
                       ent->d_name, version);
            }
        } else {
            printf("  %s@%s: ok (%d files)\n", ent->d_name, version, file_count);
        }
        checked++;
    }
    closedir(d);

    if (package_name && checked == 0) {
        fprintf(stderr, "xs update: package '%s' not installed\n", package_name);
        return 1;
    }
    printf("checked %d package(s), %d updated\n", checked, updated);
    return 0;
}

int pkg_list(void) {
    DIR *d = opendir("xs_modules");
    if (!d) {
        printf("no xs_modules/ directory found\n");
        return 0;
    }
    struct dirent *ent;
    int count = 0;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "xs_modules/%s", ent->d_name);
        DIR *sub = opendir(fullpath);
        if (sub) {
            closedir(sub);
            char tomlpath[2048];
            snprintf(tomlpath, sizeof(tomlpath), "%s/xs.toml", fullpath);
            char version[256] = "unknown";
            read_pkg_version(tomlpath, version, sizeof(version));
            printf("  %s@%s\n", ent->d_name, version);
            count++;
        }
    }
    closedir(d);
    if (count == 0) printf("no packages installed\n");
    return 0;
}

/* xs publish */
int pkg_publish(const char *path) {
    const char *pkg_dir = path ? path : ".";

    char tomlpath[1024];
    snprintf(tomlpath, sizeof(tomlpath), "%s/xs.toml", pkg_dir);
    if (!file_exists(tomlpath)) {
        fprintf(stderr, "xs publish: no xs.toml found in '%s'\n", pkg_dir);
        return 1;
    }

    char name[256] = "";
    char version[256] = "";

    FILE *f = fopen(tomlpath, "r");
    if (!f) {
        fprintf(stderr, "xs publish: cannot read '%s'\n", tomlpath);
        return 1;
    }
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        /* Parse name */
        char *p = strstr(line, "name");
        if (p && !name[0]) {
            p = strchr(p, '=');
            if (p) {
                p++;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == '"') {
                    p++;
                    char *end = strchr(p, '"');
                    if (end) {
                        size_t len = (size_t)(end - p);
                        if (len >= sizeof(name)) len = sizeof(name) - 1;
                        memcpy(name, p, len);
                        name[len] = '\0';
                    }
                }
            }
        }
        /* Parse version */
        p = strstr(line, "version");
        if (p && !version[0]) {
            p = strchr(p, '=');
            if (p) {
                p++;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == '"') {
                    p++;
                    char *end = strchr(p, '"');
                    if (end) {
                        size_t len = (size_t)(end - p);
                        if (len >= sizeof(version)) len = sizeof(version) - 1;
                        memcpy(version, p, len);
                        version[len] = '\0';
                    }
                }
            }
        }
    }
    fclose(f);

    /* Validate required fields */
    if (!name[0]) {
        fprintf(stderr, "xs publish: xs.toml missing 'name' field\n");
        return 1;
    }
    if (!version[0]) {
        fprintf(stderr, "xs publish: xs.toml missing 'version' field\n");
        return 1;
    }

    /* Check that the package has source files */
    int file_count = count_pkg_files(pkg_dir);
    if (file_count <= 1) {
        fprintf(stderr, "xs publish: package '%s' has no source files\n", name);
        return 1;
    }

    /* Create tarball */
    char tarball[1024];
    snprintf(tarball, sizeof(tarball), "%s-%s.tar.gz", name, version);
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "tar czf %s -C %s . 2>&1", tarball, pkg_dir);
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "xs publish: failed to create tarball '%s'\n", tarball);
        return 1;
    }

    printf("Package validated:\n");
    printf("  name:    %s\n", name);
    printf("  version: %s\n", version);
    printf("  files:   %d\n", file_count);
    printf("  tarball: %s\n", tarball);
    printf("\nNo registry configured: package created locally as '%s'\n", tarball);
    printf("To publish, upload this tarball to your package registry.\n");
    return 0;
}
