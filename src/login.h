#ifndef LOGIN_H
#define LOGIN_H

#include <stdbool.h>

/* إيجاد رقم الـ virtual terminal الحر التالي */
int get_next_free_vt(void);

/* التبديل إلى virtual terminal معين (بالاسم مثل "/dev/tty2") */
void switch_vt(const char *tty_name);

/* محاولة تسجيل الدخول عبر PAM وإطلاق الجلسة */
bool attempt_login(bool is_autologin);

/* تنظيف موارد الرسومات (DRM / GTK) */
void cleanup_graphics(void);

#endif /* LOGIN_H */
