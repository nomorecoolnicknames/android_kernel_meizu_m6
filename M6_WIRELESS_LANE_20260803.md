# M6 wireless lane — Wi-Fi supplicant HIDL + Bluetooth root cause — 2026-08-03

Agent: wireless-lane. Device: meizu_m6 `711HEBRN23L3N` (read-only в этом цикле).
ROM tree: `/home/gun/m6rom16/rom` (west), out `/home/gun/m6-out16`.
Kernel tree: `/srv/forge/android/meizu_m6/kernel-meizu_M6-N-ex6/kernel-3.18`.
Артефакты: `/home/gun/m6wl-out/` (west), см. `MANIFEST.txt` там же.
Продолжение: `M6_LOS16_BRINGUP_STATE.md` «2026-08-03 (part 2)», пункт 10.

---

## 1. Wi-Fi — последний блокер закрыт в дереве, остался только шаг установки

### Блокер (входное состояние)
**FACT** (logcat 2026-08-03): `hwservicemanager: getTransport: Cannot find entry
android.hardware.wifi.supplicant@1.0::ISupplicant/default in either framework or device
manifest` → `SupplicantStaIfaceHal` не биндится → `WifiNative: Failed to connect to
supplicant` → ClientMode сворачивается, чип выключается.

### Установленные факты
1. **FACT.** Источник ramdisk-файла `root/init.mt6750.rc` — это
   `device/meizu/m3_meizu_m6-common/rootdir/init.mt6755.rc`, копируется с переименованием
   правилом `device/meizu/meizu_m6/device_meizu_m6.mk:339`. Никакого отдельного
   `init.mt6750.rc` в дереве нет.
2. **FACT.** Device-манифест M6 ставится как `/system/vendor/manifest.xml` через
   `device_meizu_m6.mk:59` (`$(LOCAL_PATH)/manifest.xml:$(TARGET_COPY_OUT_VENDOR)/manifest.xml`).
   До правки device == tree == out, md5 `b6627b736ef7c168819ba10f6f66e5f5`; записи
   supplicant там не было (только `android.hardware.wifi`).
3. **FACT.** Собранный `wpa_supplicant` скомпилирован с HIDL:
   `external/wpa_supplicant_8/wpa_supplicant/Android.mk:1778` `HIDL_INTERFACE_VERSION = 1.1`;
   бинарь линкует `android.hardware.wifi.supplicant@1.0.so` и `@1.1.so` (strings по
   `out/.../system/vendor/bin/hw/wpa_supplicant`). Т.е. демон регистрирует
   `ISupplicant@1.1/default`; манифест с версией 1.1 покрывает и запрос @1.0 из лога
   (libvintf матчит одинаковый major + minor ≥ запрошенного).
4. **FACT.** Регистрация сервиса в hwservicemanager P VINTF-ом не гейтится (проверен
   `system/hwservicemanager/ServiceManager.cpp` — манифест участвует только в
   `getTransport`), т.е. блокировала именно клиентская сторона getService.
5. **FACT — скрытый следующий блокер, закрыт той же правкой.** Сервис стартует с
   `-c/data/misc/wifi/wpa_supplicant.conf`, но на Pie этот файл НИКТО не создаёт
   (O-шный `ensure_config_file_exists()` из libwifi-system удалён), а отсутствие -c
   файла фатально: `wpa_supplicant.c:5185-5189` (`wpa_config_read` → NULL → return -1)
   → демон выходит до регистрации HIDL. `system/core/rootdir/init.rc:444-452` создаёт
   каталоги и делает chmod conf-файла, но не создаёт сам файл. Шаблон
   `/system/etc/wifi/wpa_supplicant.conf` на устройстве есть (md5
   `f2cd6fe534ae2b6b526ebd80034783fe`, совпадает с out).
6. **FACT.** Ставить бинарники не нужно: `wpa_supplicant`
   (`ad916f4db82883e84de4bbaa3dd00356`), `supplicant@1.0.so`
   (`f5c9ac7b7e0f721d846761c55131a62f`), `@1.1.so` (`935bb6cb3c7ca3d7b137345dec8da9c8`)
   на устройстве побайтно равны out.
7. **FACT.** SELinux на устройстве Permissive (`getenforce`) — sepolicy для supplicant
   в этом цикле не блокер.

### Правки (обе уже в дереве на west)
- `device/meizu/meizu_m6/manifest.xml` — добавлен блок
  `android.hardware.wifi.supplicant` / hwbinder / **1.1** / `ISupplicant default`
  (с комментарием-обоснованием). Новый md5 `32bd2e2a43986ab7047571fcc2d633b2`.
- `device/meizu/m3_meizu_m6-common/rootdir/init.mt6755.rc` — (a) сервису
  `wpa_supplicant` добавлены `interface android.hardware.wifi.supplicant@1.0::ISupplicant
  default` и `...@1.1::ISupplicant default`; (b) в `on post-fs-data` — посев
  `copy /system/etc/wifi/wpa_supplicant.conf /data/misc/wifi/wpa_supplicant.conf` +
  `chown wifi wifi` + `chmod 0660`. Новый md5 `5f18bfc68bc0228fd1d089b5f0af0f9b`.
  Порядок старта сознательно не менялся (disabled+oneshot, поднимает framework после
  загрузки драйвера HAL-ом — требование «supplicant не раньше HAL» соблюдено).
  `p2p_supplicant` не тронут (Pie-фреймворк использует один сервис `wpa_supplicant`).

### Установка (ведущему)
1. `/home/gun/m6wl-out/manifest.xml` → `/system/vendor/manifest.xml` — TWRP, обычный
   протокол (md5-проверка, chcon `u:object_r:system_file:s0`). boot.img НЕ нужен.
2. **ramdisk**: `root/init.mt6750.rc` — нужна пересборка boot.img (`make bootimage`
   подхватит правленый источник; готовая копия файла лежит в
   `/home/gun/m6wl-out/init.mt6750.rc` для ручного репака).

### Маркеры следующего цикла
- Позитив: исчезает `Cannot find entry ...ISupplicant/default`;
  `init.svc.wpa_supplicant=running` при включении Wi-Fi; в `dumpsys wifi` появляются
  scan results / ассоциация.
- Негатив (проверять при провале): `Failed to read or parse configuration` (посев conf
  не сработал); мгновенный exit сервиса (смотреть `logcat -b main -s wpa_supplicant`).
- Известное «дальше, не в этом цикле»: SoftAP требует `android.hardware.wifi.hostapd`
  (не задекларирован); p2p не прорабатывался.

---

## 2. Bluetooth — root cause найден и совпадает с m681; патчи подготовлены

### Переинтерпретация входных «фактов»
- **REJECTED: «status 1 = Unknown HCI Command».** `status=%d` в
  `M2NOTE_BT_FW_STEP_TRACE stage=done` печатает ВНУТРЕННИЙ `cmd_status` HAL-а
  (`radiomod.c:346-347`), а enum — `CMD_SUCCESS=0, CMD_FAIL=1`
  (`vendor/mediatek/hidl/bluetooth/bt_mtk.h:63-64`). Это не HCI-статус контроллера.
  CMD_FAIL ставится двумя путями: таймаут 5 с (`radiomod.c:108-111`, при этом ОБЯЗАН
  печататься `M2NOTE_BT_FW_WAIT_TIMEOUT_TRACE`) или событие с плохим статусом
  (`radiomod.c:828`, при этом печатается `M2NOTE_BT_FW_EVENT_TRACE handler=common ...`).
- **REJECTED: «chip=0x337 vs chipId=0x0326 — зацепка».** `0x337` — жёстко зашитая
  константа `M2NOTE_BT_SOC_CHIP_ID 0x0337` (`radiomod.c:67`, используется в :406),
  ничего из железа не читает; GORM-скрипт выбирается БЕЗУСЛОВНО
  (`radiomod.c:252` `cur_script = bt_init_preload_script_soc`), а «default=6735» —
  это только NVRAM-фолбэк `stBtDefault_6735` (`radiomod.c:430-434`), который не
  задействован (NVRAM читается). Расхождение — «константа в порте vs регистр WMT»,
  диагностической силы не имеет. (Комментарий в BT rc «status 1 = Unknown HCI Command
  ... start there» этой записью СНИМАЕТСЯ; сам rc не переписывал.)

### Цепочка root cause (каждое звено — источник или наблюдение)
1. **FACT.** Pie BT HAL (`vendor/mediatek/hidl/bluetooth/vendor_interface.cc:192`)
   открывает `/dev/stpbt` НАПРЯМУЮ (O_NONBLOCK) и вешает на него `H4Protocol`
   (:227-231). GORM-инициализация шлёт команды через `transmit_old`
   (:91-113) → `VendorInterface::Send` (:285,303) → `H4Protocol::Send` →
   **`writev(uart_fd_, iov[2])`** (`h4_protocol.cc:49`): [байт типа H4][payload].
2. **FACT.** В ядре M6 3.18 у `BT_fops` НЕТ `.write_iter` (`stp_chrdev_bt.c:397-406`
   до патча), а `fs/read_write.c:830-842` при отсутствии `write_iter`/`aio_write`
   уходит в `do_loop_readv_writev` → **один writev = два вызова `BT_write`** (1 байт,
   затем 3), и `BT_write` шлёт один STP-пакет на вызов (`stp_chrdev_bt.c:206`,
   `mtk_wcn_stp_send_data` без коалесинга).
3. **FACT (m681, проверено на железе).** Ровно эта пара пакетов (`Tx-len:1 0x01` +
   `Tx-len:3 03 0c 00`) детерминированно валит транспортный парсер прошивки connsys:
   `<ASSERT> system/transport/hcit_mtk_stp.c #2171`
   (`/srv/forge/android/m681/docs/LANE_BT_LOS16_20260727.md:1690-1799`), и фикс
   `.write_iter` (их коммит `8b7ed757`) устранил это на устройстве (btfix7: BT rx
   0 → 431 пакет).
4. **FACT (M6, наблюдение из crash-storm).** Тот же самый
   `<ASSERT> ... hcit_mtk_stp.c #2171` зарегистрирован на M6 при каждом рестарте BT
   (`M6_LOS16_BRINGUP_STATE.md:1399-1401`).
5. **FACT.** Абортное сообщение M6 `OnDataReady: Read packet type error: Socket
   operation on non-socket` объясняется дословно: на whole-chip reset `BT_read`
   возвращает выдуманный код **-88** (`stp_chrdev_bt.c:237`), userspace видит errno
   88 = ENOTSOCK, `LOG_ALWAYS_FATAL` печатает его strerror → SIGABRT → респаун ~5 с.
   `BT_poll` при rstflag держит fd вечно readable (`stp_chrdev_bt.c:160-162`) —
   отсюда невыходящий цикл poll→read→abort.
6. **INFERENCE (на фактах 1-5).** Первый же `HCI_Reset` уходит разрезанным → прошивка
   в ассерте → ответа нет → 5-секундный таймаут GORM → `stage=done index=0
   opcode=0x0c03 status=1` (CMD_FAIL) → whole-chip reset → -88/ENOTSOCK → SIGABRT
   → рестарт → снова разрезанный HCI_Reset. Wi-Fi валится тем же whole-chip reset.
   Один дефект ядра объясняет ВСЕ наблюдённые симптомы M6.

### Гипотезы и проверки (по правилам ≥3)
- **H1 (ведущая, = INFERENCE выше): writev-split.** Проверки: (а) прошить ядро с
  патчем 01, снять `disabled` с BT HAL → ожидание: `stage=done index=0 opcode=0x0c03
  status=0`, `M2NOTE_BT_FW_EVENT_TRACE handler=common event=0x0e opcode=0x0c03
  status=0 success=1`, ноль `<ASSERT>` в dmesg, wlan0 живёт при включённом BT.
  (б) Опровержение без прошивки: если в crash-storm логе БЫЛ
  `M2NOTE_BT_FW_EVENT_TRACE ... opcode=0x0c03` (ответ приходил) — H1 мертва.
  Старый логcat уже ротирован, лог этого цикла не сохранился — проверка (б)
  выполнима только при повторном (незапатченном) прогоне, специально его не гонял,
  чтобы не возвращать crash-storm.
- **H2: контроллер отвечает, но с ошибкой** (CMD_FAIL по событию — прошивка
  отвергает HCI_Reset в текущем состоянии). Дискриминатор тот же: наличие/отсутствие
  `M2NOTE_BT_FW_WAIT_TIMEOUT_TRACE` (таймаут) vs `M2NOTE_BT_FW_EVENT_TRACE` (ответ)
  в одном прогоне. Патченный прогон тоже решает: если после фикса `status=1`
  остаётся И появляется EVENT_TRACE со status≠0 — истинна H2, копать статус.
- **H3: не залит/битый патч прошивки connsys** (контроллер в ROM-состоянии). Проверка:
  на включении BT в dmesg смотреть WMT patch-маркеры (`srh_patch`, HVer/SVer);
  на m681 патч верифицировался чистым — ожидаю то же; опровержение: patch check pass.
- **H4: неверный GORM-скрипт из-за chip=0x337.** REJECTED источником:
  выбор скрипта безусловный (`radiomod.c:252`), см. выше.

### Подготовленный фикс (НЕ собран в образ, НЕ прошит — решение за ведущим)
- `01-stp_chrdev_bt-write_iter.patch` — первичный: `BT_write_iter()` собирает весь
  iovec одним `copy_from_iter` в `o_buf` под `wr_mtx` и шлёт ОДИН STP-пакет; общий
  хвост вынесен в `bt_send_locked()`; `.write_iter` зарегистрирован в `BT_fops`.
  Порт m681 `8b7ed757` на 3.18 (3.18 API проверен: `fs.h:1508`, `uio.h:84,95`).
- `00-stp_chrdev_bt-combined-01+02.patch` — 01 + гигиена reset-пути: вместо -88
  один корректный HCI Hardware Error event `04 10 01 00` с уважением count читателя
  (курсор `rst_evt_pos`, взводится в `bt_cdev_rst_cb` на RESET_START) и POLLIN
  гейтится после доставки — SIGABRT-петля при любом будущем whole-chip reset
  превращается в чистый рестарт стека. Аналог m681 `1ca10c80`+`41e1e628` (у M6 3.18
  синтеза события не было вовсе — был только -88).
- **Верификация компиляции (FACT):** оба изменения применены в рабочем дереве и
  собраны ТОЧНОЙ боевой командой компилятора (реплей
  `out-rot0/.../.stp_chrdev_bt.o.cmd`, включая `-Werror` из
  `drivers/misc/mediatek/Makefile:16`): exit 0, ноль предупреждений; в объекте
  присутствуют `BT_write_iter` (t), `HCI_EVT_HW_ERROR` (r), `rst_evt_pos` (d)
  (nm). `out-rot0` НЕ ТРОНУТ (объект в scratch, mtime боевого .o остался 13:00).
- **Состояние дерева:** комбинированный патч ОСТАВЛЕН ПРИМЕНЁННЫМ (uncommitted) в
  `kernel-3.18/drivers/misc/mediatek/connectivity/common/common_main/linux/stp_chrdev_bt.c`.
  Это безопасно даже если другой lane пересоберёт ядро: BT HAL сейчас `disabled`,
  изменение инертно без него. Откат: `git checkout -- kernel-3.18/.../stp_chrdev_bt.c`.
  Патч-файлы также в `/home/gun/m6wl-out/`.
- **Порядок включения BT (после решения ведущего):** (1) пересобрать ядро из
  правленого дерева и прошить boot.img (можно одним циклом с ramdisk-правкой Wi-Fi —
  ядро и ramdisk едут в одном образе); (2) только после этого снять `disabled` в
  `vendor/mediatek/hidl/bluetooth/android.hardware.bluetooth@1.0-service.mtk.rc`
  (ставится в /vendor как runtime rc — отдельная TWRP-установка, boot.img не нужен).
  Снятие `disabled` ДО прошивки ядра вернёт crash-storm и убьёт Wi-Fi.
- Бонус: `system/bt` общего дерева уже несёт m681-фиксы `b6e2301` (терпимость к
  отказу ext-features page 1) и `3a24f62` (восстановление BLE) — следующая стена
  m681 для M6 предзакрыта.

## 3. GPS/FM
Не трогал (приоритет Wi-Fi/BT). Из наблюдений: `android.hardware.gnss@1.0-service`
в out собран; FM-радио драйвер в connsys есть (fmradio в дереве ядра). Отдельного
разбора не делалось.
