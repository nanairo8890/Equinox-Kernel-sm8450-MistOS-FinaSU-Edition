// SPDX-License-Identifier: GPL-2.0
/*
 * kmicheck: tells, before flashing, whether the ROM's vendor modules will load
 * on this kernel.
 *
 *   kmicheck [-u] [-m vermagic] vmlinux.symvers <dir or .ko>...
 *
 * It checks every module the way the module loader does: each __versions entry
 * for a symbol the kernel exports must carry the kernel's CRC (MODVERSIONS),
 * the vermagic after the release string must match the kernel's (-m), and no
 * module may export a symbol the kernel already exports. With -u, every
 * undefined, non-weak symbol the module was built with a version for (that is,
 * one the ROM's kernel or modules exported) must also be exported by this
 * kernel or by one of the given modules, so pass -u only when all of the ROM's
 * modules are given. Symbols that had no version when the module was built
 * already failed to resolve on the ROM's own kernel, so they are not counted.
 * Modules named in a directory's modules.blocklist are never loaded by Android,
 * so they are not checked.
 *
 * Exit status: 0 compatible, 1 incompatible, 2 usage, read error or no modules.
 *
 * It uses no libc, so it runs in any recovery or Android userspace:
 *   clang --target=aarch64-linux-gnu -Os -ffreestanding -fno-builtin \
 *         -fno-stack-protector -nostdlib -static -fuse-ld=lld -o kmicheck kmicheck.c
 */

#if !defined(__aarch64__) || !defined(__linux__)
#error "kmicheck is built for aarch64 Linux"
#endif

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;

#define MAX_MODULES	8192
#define HASH_BITS	17
#define MAX_REPORTS	25

/* ---- syscalls ---- */

static long sys6(long nr, long a, long b, long c, long d, long e, long f)
{
	register long x8 asm("x8") = nr;
	register long x0 asm("x0") = a;
	register long x1 asm("x1") = b;
	register long x2 asm("x2") = c;
	register long x3 asm("x3") = d;
	register long x4 asm("x4") = e;
	register long x5 asm("x5") = f;

	asm volatile("svc #0"
		     : "+r"(x0)
		     : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
		     : "memory");
	return x0;
}

#define AT_FDCWD	-100
#define ENOTDIR		20
#define DT_UNKNOWN	0
#define DT_REG		8
#define DT_LNK		10

static long sys_openat(const char *path) { return sys6(56, AT_FDCWD, (long)path, 0, 0, 0, 0); }
static long sys_close(long fd) { return sys6(57, fd, 0, 0, 0, 0, 0); }
static long sys_getdents64(long fd, void *buf, long len) { return sys6(61, fd, (long)buf, len, 0, 0, 0); }
static long sys_lseek_end(long fd) { return sys6(62, fd, 0, 2, 0, 0, 0); }
static long sys_write(long fd, const void *buf, long len) { return sys6(64, fd, (long)buf, len, 0, 0, 0); }
static long sys_mmap(long len, long prot, long flags, long fd) { return sys6(222, 0, len, prot, flags, fd, 0); }
static void sys_exit(long code) { sys6(94, code, 0, 0, 0, 0, 0); __builtin_unreachable(); }

static int sys_failed(long ret) { return (unsigned long)ret > -4096UL; }

/* clang may emit calls to these even with -fno-builtin */
void *memset(void *s, int c, unsigned long n)
{
	u8 *p = s;

	while (n--)
		*p++ = (u8)c;
	return s;
}

void *memcpy(void *d, const void *s, unsigned long n)
{
	u8 *dp = d;
	const u8 *sp = s;

	while (n--)
		*dp++ = *sp++;
	return d;
}

static void *alloc(unsigned long len)
{
	long p = sys_mmap(len, 3 /* RW */, 0x22 /* PRIVATE|ANONYMOUS */, -1);

	return sys_failed(p) ? 0 : (void *)p;
}

/* path names */
static char *alloc_small(unsigned long len)
{
	static char *arena;
	static unsigned long left;
	char *p;

	if (len > left) {
		left = len > (1 << 20) ? len : 1 << 20;
		arena = alloc(left);
		if (!arena)
			return 0;
	}
	p = arena;
	arena += len;
	left -= len;
	return p;
}

static const u8 *map_file(const char *path, u64 *size)
{
	long fd = sys_openat(path), len, p;

	if (sys_failed(fd))
		return 0;
	len = sys_lseek_end(fd);
	if (sys_failed(len) || len == 0) {
		sys_close(fd);
		return 0;
	}
	p = sys_mmap(len, 1 /* READ */, 2 /* PRIVATE */, fd);
	sys_close(fd);
	if (sys_failed(p))
		return 0;
	*size = len;
	return (const u8 *)p;
}

/* ---- strings and output ---- */

static unsigned long slen(const char *s)
{
	unsigned long n = 0;

	while (s[n])
		n++;
	return n;
}

static int seq(const char *a, unsigned long alen, const char *b, unsigned long blen)
{
	if (alen != blen)
		return 0;
	while (alen--)
		if (*a++ != *b++)
			return 0;
	return 1;
}

static int is(const char *a, const char *b) { return seq(a, slen(a), b, slen(b)); }

static char outbuf[4096];
static unsigned long outlen;

static void flush(void)
{
	if (outlen)
		sys_write(1, outbuf, outlen);
	outlen = 0;
}

static void putn(const char *s, unsigned long n)
{
	while (n--) {
		if (outlen == sizeof(outbuf))
			flush();
		outbuf[outlen++] = *s++;
	}
}

static void puts_(const char *s) { putn(s, slen(s)); }

static void putdec(u64 v)
{
	char b[24];
	int i = sizeof(b);

	do {
		b[--i] = '0' + v % 10;
		v /= 10;
	} while (v);
	putn(b + i, sizeof(b) - i);
}

static void puthex(u64 v)
{
	char b[16];
	int i = sizeof(b);

	do {
		b[--i] = "0123456789abcdef"[v & 15];
		v >>= 4;
	} while (v);
	while (i > 8)
		b[--i] = '0';
	putn("0x", 2);
	putn(b + i, sizeof(b) - i);
}

/* ---- symbol tables ---- */

struct sym {
	const char *name;
	u32 len;
	u32 crc;
};

static struct sym *kernel, *exported;
static u32 nkernel, nexported;

static struct sym *lookup(struct sym *t, const char *name, unsigned long len)
{
	u32 mask = (1u << HASH_BITS) - 1, h = 2166136261u;
	unsigned long n;

	for (n = 0; n < len; n++)
		h = (h ^ (u8)name[n]) * 16777619u;
	for (h &= mask;; h = (h + 1) & mask)
		if (!t[h].name || seq(t[h].name, t[h].len, name, len))
			return &t[h];
}

static int add(struct sym *t, u32 *count, const char *name, unsigned long len, u32 crc)
{
	struct sym *s = lookup(t, name, len);

	if (s->name)
		return 0;
	if (*count >= (1u << HASH_BITS) / 2)
		return -1;
	s->name = name;
	s->len = len;
	s->crc = crc;
	(*count)++;
	return 0;
}

/* vmlinux.symvers / Module.symvers: 0xcrc \t symbol \t vmlinux \t export type [\t namespace] */
static int load_symvers(const char *path)
{
	u64 size, i = 0;
	const char *p = (const char *)map_file(path, &size);

	if (!p)
		return -1;
	while (i < size) {
		u64 name, len;
		u32 crc = 0;

		if (size - i > 2 && p[i] == '0' && p[i + 1] == 'x') {
			for (i += 2; i < size && p[i] != '\t'; i++)
				crc = crc << 4 | (p[i] <= '9' ? p[i] - '0' : (p[i] | 0x20) - 'a' + 10);
			name = ++i;
			while (i < size && p[i] != '\t' && p[i] != '\n')
				i++;
			len = i - name;
			if (len && size - i > 8 && seq(p + i + 1, 7, "vmlinux", 7) && p[i + 8] == '\t' &&
			    add(kernel, &nkernel, p + name, len, crc))
				return -1;
		}
		while (i < size && p[i] != '\n')
			i++;
		i++;
	}
	return nkernel ? 0 : -1;
}

/* ---- modules ---- */

struct shdr {
	u32 name, type;
	u64 flags, addr, offset, size;
	u32 link, info;
	u64 addralign, entsize;
};

struct module {
	const char *path;
	const u8 *data;
	u64 size;
	const struct shdr *sh;
	u32 shnum;
	const char *shstr;
	u64 shstrsize;
	int blocklisted;
};

static struct module mods[MAX_MODULES];
static u32 nmods;

static u64 rd(const u8 *p, int n)
{
	u64 v = 0;

	while (n--)
		v = v << 8 | p[n];
	return v;
}

static int load_module(const char *path)
{
	struct module *m = &mods[nmods];
	const struct shdr *str;
	const u8 *d;
	u64 size, shoff, shnum, shstrndx;

	if (nmods == MAX_MODULES || !(d = map_file(path, &size)))
		return -1;
	/* ELF64, little endian, relocatable, AArch64 */
	if (size < 64 || !seq((const char *)d, 4, "\177ELF", 4) || d[4] != 2 || d[5] != 1 ||
	    rd(d + 16, 2) != 1 || rd(d + 18, 2) != 183 || rd(d + 0x3a, 2) != sizeof(struct shdr))
		return -1;
	shoff = rd(d + 0x28, 8);
	shnum = rd(d + 0x3c, 2);
	shstrndx = rd(d + 0x3e, 2);
	if (shoff > size || (shoff & 7) || shnum > (size - shoff) / sizeof(struct shdr) ||
	    shstrndx >= shnum)
		return -1;
	m->path = path;
	m->data = d;
	m->size = size;
	m->sh = (const struct shdr *)(d + shoff);
	m->shnum = shnum;
	str = &m->sh[shstrndx];
	if (str->offset > size || str->size > size - str->offset)
		return -1;
	m->shstr = (const char *)d + str->offset;
	m->shstrsize = str->size;
	nmods++;
	return 0;
}

/* the contents of a section, or 0 */
static const u8 *section(const struct module *m, const char *name, u64 *size, u32 *index)
{
	unsigned long n = slen(name) + 1;
	u32 i;

	for (i = 1; i < m->shnum; i++) {
		const struct shdr *s = &m->sh[i];

		if (s->name >= m->shstrsize || n > m->shstrsize - s->name ||
		    !seq(m->shstr + s->name, n, name, n) || s->type == 8 /* NOBITS */)
			continue;
		if (s->offset > m->size || s->size > m->size - s->offset)
			return 0;
		*size = s->size;
		if (index)
			*index = i;
		return m->data + s->offset;
	}
	return 0;
}

/* modprobe treats - and _ in module names as the same */
static int same_modname(const char *a, unsigned long alen, const char *b, unsigned long blen)
{
	if (alen != blen)
		return 0;
	while (alen--) {
		char x = *a++, y = *b++;

		if ((x == '-' ? '_' : x) != (y == '-' ? '_' : y))
			return 0;
	}
	return 1;
}

/* marks the modules loaded from dir that its modules.blocklist names ("blocklist <name>") */
static void apply_blocklist(const char *dir, unsigned long dlen, u32 first)
{
	char *path = alloc_small(dlen + sizeof("/modules.blocklist"));
	const char *p;
	u64 size, i = 0;
	u32 m;

	if (!path)
		return;
	memcpy(path, dir, dlen);
	memcpy(path + dlen, "/modules.blocklist", sizeof("/modules.blocklist"));
	p = (const char *)map_file(path, &size);
	while (p && i < size) {
		u64 end = i, name;

		while (end < size && p[end] != '\n')
			end++;
		if (end - i > 10 && seq(p + i, 10, "blocklist ", 10)) {
			for (name = i + 10; name < end && p[name] == ' '; name++)
				;
			while (end > name && (p[end - 1] == ' ' || p[end - 1] == '\r'))
				end--;
			for (m = first; m < nmods; m++) {
				const char *b = mods[m].path + dlen + 1;

				if (same_modname(b, slen(b) - 3, p + name, end - name))
					mods[m].blocklisted = 1;
			}
		}
		while (end < size && p[end] != '\n')
			end++;
		i = end + 1;
	}
}

/* a .ko file, or every .ko file in a directory: 0 on success */
static int scan_path(const char *path)
{
	static u8 buf[32768];
	unsigned long plen = slen(path);
	long fd = sys_openat(path), n;
	u32 first = nmods;

	if (sys_failed(fd))
		return -1;
	n = sys_getdents64(fd, buf, sizeof(buf));
	if (n == -ENOTDIR) {
		sys_close(fd);
		return load_module(path);
	}
	for (; n > 0; n = sys_getdents64(fd, buf, sizeof(buf))) {
		long off = 0;

		while (off < n) {
			const u8 *e = buf + off;
			const char *name = (const char *)e + 19;
			unsigned long len = slen(name);
			char *full;

			off += rd(e + 16, 2);
			if (len < 4 || !seq(name + len - 3, 3, ".ko", 3) ||
			    (e[18] != DT_REG && e[18] != DT_LNK && e[18] != DT_UNKNOWN))
				continue;
			full = alloc_small(plen + len + 2);
			if (!full)
				n = -1;
			if (n < 0)
				break;
			memcpy(full, path, plen);
			full[plen] = '/';
			memcpy(full + plen + 1, name, len + 1);
			if (load_module(full)) {
				puts_("kmicheck: not an arm64 module: ");
				puts_(full);
				puts_("\n");
				n = -1;
				break;
			}
		}
		if (n < 0)
			break;
	}
	sys_close(fd);
	if (n < 0)
		return -1;
	apply_blocklist(path, plen, first);
	return 0;
}

/* ---- checks ---- */

static u32 problems;

/* starts a problem line; returns whether the rest of it should be printed */
static int report(const struct module *m, const char *what, const char *sym, unsigned long len)
{
	const char *b = m->path, *p;

	if (++problems > MAX_REPORTS)
		return 0;
	for (p = m->path; *p; p++)
		if (*p == '/')
			b = p + 1;
	puts_(what);
	puts_(" ");
	puts_(b);
	puts_(": ");
	putn(sym, len);
	return 1;
}

/* the part of a vermagic after the release string, which MODVERSIONS ignores */
static const char *vermagic_tail(const char *v)
{
	while (*v && *v != ' ')
		v++;
	return v;
}

static void check_exports(const struct module *m)
{
	u64 size, i = 0;
	const char *s = (const char *)section(m, "__ksymtab_strings", &size, 0);

	while (s && i < size) {
		unsigned long len = 0;

		while (i + len < size && s[i + len])
			len++;
		/* this also holds namespace names, which never match a kernel symbol */
		if (len && lookup(kernel, s + i, len)->name &&
		    report(m, "DUPLICATE", s + i, len))
			puts_(" is also exported by the kernel\n");
		i += len + 1;
	}
}

static u64 check_versions(const struct module *m)
{
	u64 size, i, used = 0;
	const u8 *v = section(m, "__versions", &size, 0);

	if (!v && report(m, "UNVERSIONED", "no __versions section", 21))
		puts_("\n");
	/* struct modversion_info: unsigned long crc; char name[56] */
	for (i = 0; v && i + 64 <= size; i += 64) {
		const char *name = (const char *)v + i + 8;
		unsigned long len = 0;
		struct sym *k;

		while (len < 56 && name[len])
			len++;
		k = lookup(kernel, name, len);
		if (!k->name)
			continue;
		used++;
		if (rd(v + i, 8) != k->crc && report(m, "MISMATCH", name, len)) {
			puts_(" kernel ");
			puthex(k->crc);
			puts_(" module ");
			puthex(rd(v + i, 8));
			puts_("\n");
		}
	}
	return used;
}

static void check_vermagic(const struct module *m, const char *magic)
{
	u64 size, i;
	const char *info = (const char *)section(m, ".modinfo", &size, 0);

	for (i = 0; info && i < size; i++) {
		const char *v = info + i + 9, *tail;
		unsigned long n = 0;

		if ((i && info[i - 1]) || size - i <= 9 || !seq(info + i, 9, "vermagic=", 9))
			continue;
		for (tail = v; tail < info + size && *tail && *tail != ' '; tail++)
			;
		while (tail + n < info + size && tail[n])
			n++;
		if (!seq(tail, n, magic, slen(magic)) && report(m, "VERMAGIC", v, tail + n - v))
			puts_("\n");
		return;
	}
	if (report(m, "VERMAGIC", "none", 4))
		puts_("\n");
}

/* whether the module's __versions has an entry for the symbol */
static int has_version(const struct module *m, const char *sym, unsigned long len)
{
	u64 size, i;
	const u8 *v = section(m, "__versions", &size, 0);

	for (i = 0; v && len < 56 && i + 64 <= size; i += 64)
		if (seq((const char *)v + i + 8, len, sym, len) && !v[i + 8 + len])
			return 1;
	return 0;
}

static void check_unresolved(const struct module *m)
{
	const struct shdr *strs;
	const char *names;
	const u8 *st;
	u64 size, i;
	u32 index;

	st = section(m, ".symtab", &size, &index);
	if (!st || m->sh[index].link >= m->shnum)
		return;
	strs = &m->sh[m->sh[index].link];
	if (strs->offset > m->size || strs->size > m->size - strs->offset)
		return;
	names = (const char *)m->data + strs->offset;
	/* Elf64_Sym: st_name, st_info, st_other, st_shndx, st_value, st_size; entry 0 is null */
	for (i = 24; i + 24 <= size; i += 24) {
		const u8 *e = st + i;
		u64 name = rd(e, 4), len = 0;

		/* only undefined (SHN_UNDEF), named and non-weak symbols must resolve */
		if (rd(e + 6, 2) != 0 || name == 0 || name >= strs->size || (e[4] >> 4) == 2)
			continue;
		while (name + len < strs->size && names[name + len])
			len++;
		if (!lookup(kernel, names + name, len)->name &&
		    !lookup(exported, names + name, len)->name &&
		    has_version(m, names + name, len) &&
		    report(m, "UNRESOLVED", names + name, len))
			puts_(" is not exported by this kernel\n");
	}
}

int main(int argc, char **argv)
{
	const char *magic = 0;
	int unresolved = 0, i = 1;
	u64 used = 0, checked = 0;
	u32 m;

	for (; i < argc && argv[i][0] == '-'; i++) {
		if (is(argv[i], "-u"))
			unresolved = 1;
		else if (is(argv[i], "-m") && i + 1 < argc)
			magic = vermagic_tail(argv[++i]);
		else
			break;
	}
	if (argc - i < 2) {
		puts_("usage: kmicheck [-u] [-m vermagic] vmlinux.symvers <dir or .ko>...\n");
		flush();
		return 2;
	}

	kernel = alloc(sizeof(struct sym) << HASH_BITS);
	exported = alloc(sizeof(struct sym) << HASH_BITS);
	if (!kernel || !exported || load_symvers(argv[i])) {
		puts_("kmicheck: cannot read the kernel symbol versions in ");
		puts_(argv[i]);
		puts_("\n");
		flush();
		return 2;
	}
	for (i++; i < argc; i++) {
		if (scan_path(argv[i])) {
			puts_("kmicheck: cannot read ");
			puts_(argv[i]);
			puts_("\n");
			flush();
			return 2;
		}
	}
	if (!nmods) {
		puts_("kmicheck: no modules found\n");
		flush();
		return 2;
	}

	/* every module's exports, blocklisted or not, can satisfy another module */
	for (m = 0; m < nmods; m++) {
		u64 size, j = 0;
		const char *s = (const char *)section(&mods[m], "__ksymtab_strings", &size, 0);

		while (s && j < size) {
			unsigned long len = 0;

			while (j + len < size && s[j + len])
				len++;
			if (len && add(exported, &nexported, s + j, len, 0)) {
				puts_("kmicheck: too many exported symbols\n");
				flush();
				return 2;
			}
			j += len + 1;
		}
	}

	for (m = 0; m < nmods; m++) {
		if (mods[m].blocklisted)
			continue;
		checked++;
		check_exports(&mods[m]);
		used += check_versions(&mods[m]);
		if (magic)
			check_vermagic(&mods[m], magic);
		if (unresolved)
			check_unresolved(&mods[m]);
	}

	if (problems > MAX_REPORTS) {
		puts_("... and ");
		putdec(problems - MAX_REPORTS);
		puts_(" more\n");
	}
	puts_("kmicheck: ");
	putdec(checked);
	puts_(" modules checked (");
	putdec(nmods - checked);
	puts_(" blocklisted), ");
	putdec(used);
	puts_(" uses of ");
	putdec(nkernel);
	puts_(" kernel symbols, ");
	putdec(problems);
	puts_(problems ? " problems: INCOMPATIBLE\n" : " problems: compatible\n");
	flush();
	return problems ? 1 : 0;
}

/* the kernel starts us with sp pointing at argc, then argv */
void start(long *sp)
{
	sys_exit(main((int)sp[0], (char **)(sp + 1)));
}

asm(".globl _start\n"
    "_start:\n"
    "	mov x29, xzr\n"
    "	mov x0, sp\n"
    "	bl start\n");
