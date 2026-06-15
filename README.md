# vaxp-dm: Modern DRM/KMS Display Manager

مدير تسجيل دخول رسومي حديث، أنيق، وموفر جداً للموارد، مصمم خصيصاً لنظام تشغيل **VAXP OS**. يعمل البرنامج مباشرة على الواجهة الرسومية لنواة لينكس (DRM/KMS Framebuffer) باستخدام مكتبة **Cairo** للرسم ومكتبة **Pango** لتنسيق النصوص والخطوط، ومكتبة **PAM** للمصادقة الآمنة وإدارة الجلسات.

---

## 1. المميزات الرئيسية (Key Features)

*   **رسم رسومي كامل الدقة (DRM/KMS Framebuffer with Cairo):**
    *   يعمل مباشرة بدون خادم عرض (X11/Wayland) خارجي للواجهة الخاصة به، مما يجعله فائق السرعة ومناسباً للإقلاع المباشر.
    *   يدعم رسم خلفيات مخصصة مضببة بالدقة الكاملة للبكسل وعرض الصور الرمزية (Avatars) للمستخدمين.
    *   تنسيق نصوص احترافي وجميل للخطوط والرموز التعبيرية باستخدام **Pango** و **PangoCairo**.
*   **تبديل أجهزة الـ TTY التلقائي (TTY/VT Switching):**
    *   يتم إطلاق الجلسات الرسومية (Wayland أو X11) على TTY منفصل (مثل TTY2 أو التالي المتاح) بشكل تلقائي لإخفاء نصوص الإقلاع وسجلات النظام المزعجة.
    *   إدارة ذكية للملفات الخاصة بالـ TTY (تغيير الملكية للمستخدم الفعلي `chown` ووضع الصلاحيات الآمن `0600` ثم استعادتها للوضع الافتراضي `root:tty` و `0620` بعد خروج الجلسة).
    *   إعادة تنشيط الـ TTY الأصلي (TTY1) تلقائياً عند تسجيل الخروج ليعود مدير تسجيل الدخول للعمل بسلاسة.
*   **تكامل كامل مع PAM و systemd-logind:**
    *   مصادقة المستخدمين وإدارة الجلسات بشكل آمن ومتوافق مع معايير لينكس.
    *   تصدير متغيرات البيئة الأساسية مثل `XDG_SESSION_TYPE` و `XDG_SESSION_CLASS` و `XDG_VTNR` لتكامل مثالي مع بيئات الديسكتوب ومديري النوافذ الرسومية.
*   **مسح شامل للجلسات المتوفرة (Session Discovery):**
    *   البحث التلقائي عن واجهات Wayland و X11 في المسارات القياسية ومسارات التثبيت المحلي:
        *   `/usr/share/wayland-sessions` & `/usr/share/xsessions`
        *   `/usr/local/share/wayland-sessions` & `/usr/local/share/xsessions`
*   **لوحة إعدادات وتخصيص المظهر:**
    *   دعم تغيير ألوان المظهر (Theme) ديناميكياً من خلال قيم ألوان الـ Hex في ملف التهيئة.
    *   لوحة إعدادات منبثقة (Settings Gear Popup) لاختيار بيئة سطح المكتب المطلوبة باستخدام الفأرة أو لوحة المفاتيح.
    *   دعم تبديل لغات لوحة المفاتيح (الإنجليزية والعربية) أثناء كتابة الاسم وكلمة المرور عبر الاختصار `Alt+Shift`.
*   **وضع المحاكاة المدمج (Mock Mode):**
    *   إمكانية تشغيل واختبار الواجهة بالكامل داخل نافذة GTK+ 3 عادية دون الحاجة لصلاحيات الـ root أو تفعيل PAM الفعلي، لتسهيل التطوير واختبار الواجهات الرسومية والتصميم.

---

## 2. هيكلية ملفات المشروع (Project Structure)

*   [src/main.c](file:///home/x/Desktop/tui-display-manager/src/main.c): نقطة البداية، إدارة دورة حياة الرسوميات (KMS/DRM)، استقبال مدخلات الفأرة ولوحة المفاتيح عبر `evdev` ومفسر الإعدادات.
*   [src/pam_auth.c](file:///home/x/Desktop/tui-display-manager/src/pam_auth.c) & [src/pam_auth.h](file:///home/x/Desktop/tui-display-manager/src/pam_auth.h): التعامل مع مكتبة PAM وإنشاء الـ Conversation Callback وتهيئة جلسات المستخدمين.
*   [src/session.c](file:///home/x/Desktop/tui-display-manager/src/session.c) & [src/session.h](file:///home/x/Desktop/tui-display-manager/src/session.h): اكتشاف ملفات `.desktop` الخاصة بالواجهات الرسومية، إسقاط الصلاحيات (`setuid`/`setgid`)، إدارة الـ TTY، وإطلاق الجلسات.
*   [pam/vaxp-dm](file:///home/x/Desktop/tui-display-manager/pam/vaxp-dm): ملف تهيئة PAM الخاص بالخدمة.
*   [systemd/vaxp-dm.service](file:///home/x/Desktop/tui-display-manager/systemd/vaxp-dm.service): ملف الخدمة لـ Systemd لتشغيل البرنامج تلقائياً كمدير عرض للنظام.
*   [conf/vaxp-dm.conf](file:///home/x/Desktop/tui-display-manager/conf/vaxp-dm.conf): ملف الإعدادات العام لتعديل الخلفية، التسيير الذاتي، المظهر والألوان، وإعدادات الـ TTY.
*   [Makefile](file:///home/x/Desktop/tui-display-manager/Makefile): ملف التجميع والتركيب.

---

## 3. التثبيت والتشغيل الفعلي (Installation & Deployment)

### تثبيت الاعتماديات (Dependencies):

*   **Ubuntu / Debian / vaxp OS (Debian-based):**
    ```bash
    sudo apt update
    sudo apt install build-essential libdrm-dev libcairo2-dev libpango1.0-dev libgdk-pixbuf2.0-dev libxkbcommon-dev libgtk-3-dev libpam0g-dev
    ```
*   **Arch Linux:**
    ```bash
    sudo pacman -S base-devel libdrm cairo pango gdk-pixbuf2 libxkbcommon gtk3 pam
    ```

### تجميع وتثبيت البرنامج في مسارات النظام:

شغّل الأمر التالي داخل مجلد المشروع لتثبيت الملف التنفيذي والخدمات تلقائياً:
```bash
sudo make install
```

*المسارات المثبتة:*
*   الملف التنفيذي: `/usr/local/bin/VAXPDM`
*   إعدادات PAM: `/etc/pam.d/vaxp-dm` و `/etc/pam.d/vaxp-dm-autologin`
*   ملف الإعدادات: `/etc/vaxp-dm.conf`
*   خدمة Systemd: `/usr/lib/systemd/system/vaxp-dm.service`

### تفعيل البرنامج كـ Display Manager الافتراضي:

1.  قم بتعطيل الخدمة الحالية (مثلاً sddm أو gdm):
    ```bash
    sudo systemctl disable sddm.service
    ```
2.  قم بتفعيل خدمة `vaxp-dm`:
    ```bash
    sudo systemctl enable vaxp-dm.service
    ```
3.  اضبط النظام ليقلع للواجهة الرسومية افتراضياً:
    ```bash
    sudo systemctl set-default graphical.target
    ```
4.  أعد تشغيل الجهاز لتجربة المظهر الجديد!

---

## 4. ملف الإعدادات (/etc/vaxp-dm.conf)

يحتوي ملف الإعدادات على الخيارات التالية:

```ini
[General]
# المسار المطلق لخلفية الشاشة المجهزة مسبقاً (مفضلة أن تكون مضببة)
background_path = /home/x/.config/vaxp/background

# اسم الجلسة الافتراضية المراد تحديدها تلقائياً
default_session = aether

# تمكين الدخول التلقائي (true/false)
autologin = false
autologin_user = x

# تمكين تبديل الـ TTY للجلسات الرسومية (true/false)
tty_switching = true

# رقم الـ VT المراد التحويل إليه (0 لاختيار المنفذ الحر التالي تلقائياً، أو 2-7 لتحديد منفذ ثابت)
target_vt = 0

[Theme]
# ألوان الواجهة الرسومية بصيغة الـ Hex
color_title = #cba6f7
color_focus = #89b4fa
color_inactive = #7f8c8d
color_status = #f9e2af
```

---

## 5. وضع التطوير والمحاكاة (Mock Mode)

لاختبار الواجهة محلياً وبشكل آمن داخل سطر الأوامر دون الحاجة لصلاحيات root أو خروج من بيئتك الحالية، سيقوم البرنامج بفتح نافذة GTK+ 3 تحاكي شاشة تسجيل الدخول:
```bash
./VAXPDM --mock
```

*   استخدم زر `TAB` أو الأسهم للتنقل بين العناصر (اسم المستخدم، كلمة المرور، زر التروس للإعدادات، أزرار الطاقة).
*   عند الوقوف على حقل اسم المستخدم، يمكنك استخدام الأسهم يميناً ويساراً للتنقل بين المستخدمين المتاحين في النظام.
*   اضغط زر الفأرة أو `Enter` على زر التروس الأسفل لفتح قائمة الجلسات واختيار الواجهة المطلوبة بالأسهم ثم اضغط `Enter` مجدداً للتأكيد.
*   اضغط `ESC` للخروج من المحاكاة الفورية.
