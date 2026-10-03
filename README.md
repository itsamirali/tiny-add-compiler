# tiny-add-compiler

یک کامپایلر خیلی کوچک و **کاملاً شفاف** برای زبان آموزشی **TinyAdd**، نوشته‌شده به زبان **C (استاندارد C11)**.
این پروژه برای درس **طراحی کامپایلر** ساخته شده است. هدف ساختن یک زبان بزرگ نیست؛ هدف این است که **تمام مراحل کامپایل قدم‌به‌قدم دیده و توضیح داده شوند.**

> وضعیت: **کامل** — هر ۷ مرحله پیاده‌سازی و تست شده‌اند (۷۰ تست، همه موفق، بدون هیچ warning).

---

## زبان TinyAdd

TinyAdd فقط یک کار می‌کند: **جمع کردن یک لیست از اعداد صحیح مثبت.**

```
2 + 3
1 + 2 + 3 + 4
42
```

- فقط اعداد صحیح مثبت در مبنای ۱۰ و فقط عملگر `+`.
- فاصله، tab و خط جدید نادیده گرفته می‌شوند.
- صفرهای اول عدد مجازند (`007` یعنی ۷).
- عدد `0` و عددهایی که در `uint32` جا نمی‌شوند (یا مجموعی که سرریز کند) توسط فاز semantic رد می‌شوند.

## مراحل کامپایلر

```
متن برنامه
  → [1] Lexer            → توکن‌ها
  → [2] Parser           → درخت AST
  → [3] Semantic         → AST بررسی‌شده
  → [4] IR generation    → کد سه‌آدرسه (TAC)
  → [5] Optimizer        → TAC بهینه‌شده (constant folding)
  → [6] Code generation  → بایت‌کد پشته‌ای
  → [7] VM               → نتیجه
```

- توضیح کامل هر مرحله: [`docs/PHASES.md`](docs/PHASES.md)
- مشخصات کامل پروژه و همه‌ی تصمیم‌ها: [`docs/SPEC.md`](docs/SPEC.md)
- سناریوی ارائه در کلاس: [`docs/PRESENTATION.md`](docs/PRESENTATION.md)

---

## نیازمندی‌ها

- کامپایلر C: `gcc` یا `clang`.
- `make`.
- اختیاری: `valgrind` برای بررسی حافظه.

روی Ubuntu / WSL:

```sh
sudo apt update
sudo apt install build-essential valgrind
```

### روی Windows

- **روش پیشنهادی:** WSL با Ubuntu (دستورهای بالا).
- **بدون WSL:** با [w64devkit](https://github.com/skeeto/w64devkit) (GCC برای MinGW به همراه `make` و `sh`) هم پروژه ساخته و همه‌ی تست‌ها اجرا می‌شوند. فایل اجرایی `build/tinyadd.exe` است.
- **محدودیت:** MinGW از AddressSanitizer پشتیبانی نمی‌کند و Valgrind هم ندارد؛ پس `make debug` و Valgrind را روی Linux/macOS/WSL اجرا کنید.

## ساخت (build)

```sh
make          # ساخت build/tinyadd
make test     # ساخت و اجرای همه‌ی تست‌ها
make debug    # ساخت با -g و AddressSanitizer + UBSan در build/debug/  (فقط Linux/macOS)
make clean    # پاک کردن پوشه‌ی build/
```

همه‌چیز با `-std=c11 -Wall -Wextra -Wpedantic -Werror` ساخته می‌شود.

## اجرا

```sh
./build/tinyadd examples/ok_simple.tadd     # کامپایل و اجرای یک فایل
./build/tinyadd -e "1 + 2 + 3"              # کامپایل و اجرای کد از خط فرمان → 6
./build/tinyadd --trace -e "1 + 2 + 3"      # نمایش همه‌ی مراحل
./build/tinyadd --stage=lex -e "1 + 2"      # فقط یک مرحله: lex | parse | sem | ir | opt | code | vm
./build/tinyadd --no-opt -e "1 + 2 + 3"     # بدون بهینه‌ساز
./build/tinyadd --help                      # یا -h
```

فلگ‌های ظاهری: `--ascii` (فقط کاراکترهای ASCII، برای ترمینال‌هایی که Unicode را خوب نشان نمی‌دهند) و `--no-color` (بدون رنگ).
رنگ فقط وقتی روشن است که خروجی ترمینال باشد؛ اگر خروجی به فایل یا pipe برود یا متغیر `NO_COLOR` تنظیم شده باشد، خاموش است.

`--trace` و `--stage` را نمی‌شود با هم داد.

### نمونه‌ی خروجی `--trace`

```
$ ./build/tinyadd --trace -e "1 + 2 + 3"
═══ Source ═══
1 + 2 + 3

═══ [1] Lexer ═══
 #  KIND     LEXEME  LINE:COL
 0  NUMBER   1       1:1
 1  PLUS     +       1:3
 2  NUMBER   2       1:5
 3  PLUS     +       1:7
 4  NUMBER   3       1:9
 5  EOF              1:10

═══ [2] Parser (AST) ═══
Add
├── Add
│   ├── Num(1)
│   └── Num(2)
└── Num(3)

═══ [3] Semantic analysis ═══
✔ 3 literals checked: all > 0, all fit in uint32
✔ total sum 6 fits in uint32

═══ [4] Intermediate code (TAC) ═══
t1 = 1 + 2
t2 = t1 + 3
return t2

═══ [5] Optimizer (constant folding) ═══
folded: 1 + 2 → 3
folded: 3 + 3 → 6
return 6

═══ [6] Code generation (bytecode) ═══
0000  PUSH 6
0001  RET

═══ [7] VM execution ═══
0000 PUSH 6   stack: [6]
0001 RET      stack: []

Result: 6
```

### نمونه‌ی پیام خطا

```
$ ./build/tinyadd -e "1 + + 2"
error[parser]: expected NUMBER after '+', found '+'
  --> 1:5
   |
 1 | 1 + + 2
   |     ^
```

خطاها در `stderr` و نتیجه و trace در `stdout` چاپ می‌شوند. کامپایلر با اولین خطا متوقف می‌شود.

کدهای خروج: `0` موفق، `1` خطای کامپایل (lexer / parser / semantic)، `2` خطای استفاده، خطای VM یا خطای داخلی.

## تست‌ها

```sh
make test
```

- تست‌های واحد برای هر مرحله: `tests/test_*.c`.
- تست‌های سرتاسری: هر فایل `tests/cases/NAME.tadd` با فایل `NAME.expected` مقایسه می‌شود. خط اول `.expected` به شکل `exit: N` است و بعد خروجی دقیق (نتیجه یا متن خطا) می‌آید.
- برای اضافه کردن یک تست سرتاسری: دو فایل بالا را بسازید و نام آن را به آرایه‌ی `CASES` در `tests/test_vm.c` اضافه کنید.

بررسی حافظه (روی Linux):

```sh
make debug
./build/debug/test_runner                                   # همه‌ی تست‌ها زیر ASan + UBSan
./build/debug/tinyadd --trace examples/ok_list.tadd
valgrind --leak-check=full ./build/tinyadd --trace examples/ok_list.tadd
```

## ساختار پوشه‌ها

```
docs/       مستندات (فارسی)
include/    هدرها؛ یکی برای هر ماژول
src/        پیاده‌سازی هر مرحله در یک فایل جدا
tests/      test runner، تست‌های هر مرحله و برنامه‌های نمونه‌ی تست
examples/   برنامه‌های نمونه‌ی TinyAdd (درست و دارای خطا)
```

## لایسنس

MIT — فایل [`LICENSE`](LICENSE) را ببینید.
