#ifndef CONFIG_H
#define CONFIG_H

#include <cairo.h>
#include <stdbool.h>
#include <stddef.h>

/* قراءة قيمة نصية من ملف الإعداد /etc/vaxp-dm.conf */
void read_config_string(const char *key, char *out_val, size_t max_len,
                        const char *default_val);

/* تحويل لون hex (#rrggbb) إلى قيم double 0.0–1.0 */
bool parse_hex_color(const char *hex, double *r, double *g, double *b);

/* تحميل صورة من ملف وتحويلها إلى cairo_surface_t */
cairo_surface_t *load_image(const char *filename);

/* تحميل صورة الخلفية من الإعداد أو المسار الافتراضي */
void load_background(void);

/* تحميل صورة الأفاتار (~/.face أو logo.png) */
void load_avatar(void);

/* اكتشاف مستخدمي النظام من /etc/passwd */
void discover_users(void);

#endif /* CONFIG_H */
