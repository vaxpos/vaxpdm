#ifndef DRAW_H
#define DRAW_H

#include <cairo.h>
#include <stdbool.h>

/* رسم مستطيل ذو زوايا مدوّرة */
void draw_rounded_rect(cairo_t *cr, double x, double y,
                       double w, double h, double r);

/* رسم تأثير الضوء (glow) حول مستطيل */
void draw_glow(cairo_t *cr, double x, double y,
               double w, double h, double r,
               double max_alpha, double size);

/* رسم دائرة الأفاتار / أيقونة المستخدم */
void draw_avatar(cairo_t *cr, double cx, double cy, double radius);

/* رسم زر أيقونة دائري (طاقة / إعادة تشغيل / إيقاف مؤقت) */
void draw_icon_button(cairo_t *cr, double cx, double cy, double r,
                      bool focused,
                      double fr, double fg, double fb,
                      const char *symbol);

/* رسم واجهة المستخدم الكاملة على السطح المعطى */
void draw_interface(cairo_t *cr, int width, int height);

/* رسم مؤشر الفأرة البرمجي (وضع DRM) */
void draw_cursor(cairo_t *cr, double x, double y);

#endif /* DRAW_H */
