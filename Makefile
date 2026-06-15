CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=gnu11 $(shell pkg-config --cflags libdrm cairo pangocairo gdk-pixbuf-2.0 xkbcommon gtk+-3.0 2>/dev/null || echo "")
LIBS = $(shell pkg-config --libs libdrm cairo pangocairo gdk-pixbuf-2.0 xkbcommon gtk+-3.0 2>/dev/null || echo "-ldrm -lcairo -lpangocairo-1.0 -lpango-1.0 -lgdk_pixbuf-2.0 -lxkbcommon -lgtk-3 -lgdk-3") -lpam

TARGET = VAXPDM
SRC_DIR = src
OBJ_DIR = obj

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) /usr/local/bin/$(TARGET)
	install -Dm644 pam/vaxp-dm /etc/pam.d/vaxp-dm
	install -Dm644 pam/vaxp-dm-autologin /etc/pam.d/vaxp-dm-autologin
	install -Dm644 conf/vaxp-dm.conf /etc/vaxp-dm.conf
	install -Dm644 logo.png /usr/share/vaxp-dm/logo.png
	install -Dm644 systemd/vaxp-dm.service /usr/lib/systemd/system/vaxp-dm.service

uninstall:
	rm -f /usr/local/bin/$(TARGET)
	rm -f /etc/pam.d/vaxp-dm
	rm -f /etc/pam.d/vaxp-dm-autologin
	rm -f /etc/vaxp-dm.conf
	rm -rf /usr/share/vaxp-dm
	rm -f /usr/lib/systemd/system/vaxp-dm.service

.PHONY: all clean install uninstall
