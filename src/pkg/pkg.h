#ifndef XS_PKG_H
#define XS_PKG_H

int pkg_new(const char *name);
int pkg_install(const char *package_name);
int pkg_add(const char *package_name);
int pkg_remove(const char *package_name);
int pkg_update(const char *package_name);
int pkg_list(void);
int pkg_publish(const char *path);

#endif
