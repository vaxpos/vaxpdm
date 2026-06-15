# vaxp-dm: Modern TUI Display Manager

مدير تسجيل دخول رسومي-نصي (TUI) حديث وأنيق وموفر جداً للموارد، مصمم خصيصاً لنظام تشغيل **VAXP OS** باستخدام مكتبة **Notcurses** للواجهة الرسومية النصية ومكتبة **PAM** للمصادقة وإدارة الجلسات.

---

## 1. المميزات الرئيسية (Key Features)

*   **دقة خلفيات رسومية فائقة (High-Resolution Pixel Graphics):**
    *   يدعم رسم صور الخلفية (المضببة مسبقاً) بالدقة الكاملة للبكسل (Unpixelated) بدون بكسلة عبر بروتوكولات البيكسل الحديثة مثل **Kitty Graphics** أو **Sixel** عن طريق فحص دعم الطرفية بالدالة `notcurses_canpixel`.
    *   في حال عدم دعم الطرفية (كبيئة الـ Console الخام)، يعود تلقائياً لنمط الرسم النصي بنصف الخلايا (ASCII/Half-blocks) كـ Fallback آمن.
*   **تكامل كامل مع PAM و systemd-logind:**
    *   مصادقة المستخدمين وإدارة الجلسات بشكل رسمي وآمن.
    *   استدعاء `pam_open_session` لتسجيل الجلسة مع logind لتفعيل الصلاحيات (الصوت، معجل الرسوميات، إلخ) للمستخدمين العاديين.
*   **مسح شامل للجلسات المتوفرة (Session Discovery):**
    *   البحث التلقائي عن واجهات Wayland و X11 في المسارات القياسية ومسارات التثبيت المحلي:
        *   `/usr/share/wayland-sessions` & `/usr/share/xsessions`
        *   `/usr/local/share/wayland-sessions` & `/usr/local/share/xsessions` (مثل جلسة `aether`).
*   **مطلق الجلسات الآمن (Safe Session Spawner):**
    *   بدء الجلسة كعملية فرعية (Child Process) وإنشاء قائد جلسة جديد (`setsid`).
    *   إسقاط صلاحيات الـ `root` تماماً والتحول لهوية المستخدم الفعلي (`setuid`/`setgid`/`initgroups`) قبل تشغيل الواجهة.
    *   إعادة توجيه الإدخال والإخراج للـ TTY الفعلي وربطه كـ Controlling Terminal عبر `TIOCSCTTY` لضمان عمل الواجهات الرسومية بسلاسة وبدون تعليق.
*   **ملف إعدادات مخصص:**
    *   قراءة مسار الخلفية والجلسة الافتراضية ديناميكياً من ملف `/etc/vaxp-dm.conf`.
*   **وضع المحاكاة المدمج (Mock Mode):**
    *   إمكانية تشغيل واختبار الواجهة بالكامل وتجربة التنقل والكتابة داخل الطرفية العادية دون صلاحيات root وبدون تفعيل PAM الفعلي للتطوير السريع والآمن.

---

## 2. هيكلية ملفات المشروع (Project Structure)

*   `src/main.c`: نقطة البداية، محاكي التفاعل لـ Notcurses، معالج المدخلات، ومحلل ملف التهيئة.
*   `src/pam_auth.h` & `src/pam_auth.c`: التعامل مع مكتبة PAM وإنشاء الـ Conversation Callback وتهيئة الجلسات مع logind.
*   `src/session.h` & `src/session.c`: اكتشاف ملفات `.desktop` الخاصة بالواجهات الرسومية وإطلاق الجلسة المحددة بعد إسقاط الصلاحيات وإدارة الـ TTY.
*   `pam/vaxp-dm`: نموذج لملف تهيئة PAM لمدير تسجيل الدخول.
*   `systemd/vaxp-dm.service`: ملف خدمة Systemd لتشغيل البرنامج تلقائياً عند الإقلاع على TTY1.
*   `conf/vaxp-dm.conf`: ملف الإعدادات العام لتعديل الخلفية والجلسة الافتراضية.
*   `Makefile`: ملف تجميع وبناء وتثبيت المشروع.

---

## 3. التثبيت والتشغيل الفعلي (Installation & Deployment)

### تثبيت الاعتماديات (Dependencies):
*   **Ubuntu / Debian / vaxp OS (Debian-based):**
    ```bash
    sudo apt update
    sudo apt install libnotcurses-dev libpam0g-dev build-essential
    ```
*   **Arch Linux:**
    ```bash
    sudo pacman -S notcurses pam base-devel
    ```

### تجميع وتثبيت البرنامج في مسارات النظام:
شغّل الأمر التالي داخل مجلد المشروع لتثبيت الملف التنفيذي والخدمات تلقائياً:
```bash
sudo make install
```
*المسارات المثبتة:*
*   الملف التنفيذي: `/usr/local/bin/tui-dm`
*   إعدادات PAM: `/etc/pam.d/vaxp-dm`
*   ملف الإعدادات: `/etc/vaxp-dm.conf`
*   خدمة Systemd: `/usr/lib/systemd/system/vaxp-dm.service`

### تفعيل البرنامج كـ Display Manager الافتراضي:
1.  قم بتعطيل الخدمة الحالية (مثلاً sddm أو gdm):
    ```bash
    sudo systemctl disable sddm.service
    ```
2.  قم بتفعيل خدمة `vaxp-dm` (ستقوم بإنشاء symlink لـ `display-manager.service`):
    ```bash
    sudo systemctl enable vaxp-dm.service
    ```
3.  اضبط النظام ليقلع للواجهة الرسومية افتراضياً:
    ```bash
    sudo systemctl set-default graphical.target
    ```
4.  أعد تشغيل الجهاز لتجربة المظهر الجديد!

---

## 4. وضع التطوير والمحاكاة (Mock Mode)

لاختبار الواجهة محلياً وبشكل آمن داخل سطر الأوامر دون الحاجة لإعادة التشغيل أو الحصول على صلاحيات root:
```bash
./tui-dm --mock
```
*   استخدم زر `TAB` أو الأسهم للتنقل.
*   استخدم الأسهم يميناً ويساراً لتغيير الجلسة (Session) عند الوقوف عليها.
*   اضغط `ESC` للخروج الفوري من التجرية.

---

## 5. خطوات مستقبلية مقترحة للتطوير (Future Roadmap)

1.  **TTY Switching (تبديل أجهزة الـ TTY):**
    *   حالياً، يتم إطلاق الجلسة الرسومية على نفس الـ TTY (TTY1).
    *   لجعل النظام أكثر احترافية، يمكن إضافة ميزة فتح الجلسة الرسومية على TTY منفصل (مثل TTY2 أو TTY7) بمجرد نجاح التحقق، ويتم ذلك باستخدام مكالمات `ioctl` للتحويل مثل `ioctl(tty_fd, VT_ACTIVATE, vt_num)`.
2.  **دعم قراءة الألوان من الإعدادات:**
    *   توسيع محلل الإعدادات الحالي ليقوم بقراءة قيم ألوان المظهر (Theme) من المجلد الفرعي `[Theme]` في ملف التهيئة `/etc/vaxp-dm.conf` وتطبيقها ديناميكياً على صناديق Notcurses.


# vaxpdm
