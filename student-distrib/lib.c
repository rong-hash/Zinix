/* lib.c - Some basic library functions (printf, strlen, etc.)
 * vim:ts=4 noexpandtab
 */

#include "lib.h"
#include "rtc.h"
#include "page.h"
#include "terminal.h"
#include "scheduler.h"
#include "lib.h"

/* If putc is called by user
 * keystroke or by programs */
#define PUTC_USRKEY 1
#define PUTC_PROG   0

static char* video_mem = (char *)VIDEO;

/* -------------- Private helper functions -------------- */

/* void __putc(uint8_t c);
 * Inputs: uint_8* c: character to print
 *         uint8_t userkey: indicating if __putc is
 *          called by user keystroke or by programs
 * Return Value: void
 *  Function: change video memory or backup buffer */
void __putc(uint8_t c, uint8_t userkey) {
	int flags;
	int term_id;

	switch(userkey) {
		case PUTC_USRKEY: term_id = current_term_id; break;
		case PUTC_PROG: term_id = cur_sch_index; break;
		default: return;
	}

	/* Critical section begins */
	cli_and_save(flags);

	if (userkey) {
		remap_vidmap_page(current_term_id);
	}

	/* Go to a new line if get line break or if the
	 * cursor is already at the end of the current line */
	switch (c) {
		case '\0': break;
		case '\n': case '\r': handle_newline(userkey); break;
		case '\b': handle_backspace(userkey); break;
		default:
			if (terms[term_id].cursor_x >= NUM_COLS) {
				handle_newline(userkey);
			}
			*(uint8_t *)(video_mem + ((NUM_COLS * terms[term_id].cursor_y + terms[term_id].cursor_x) << 1)) = c;
			*(uint8_t *)(video_mem + ((NUM_COLS * terms[term_id].cursor_y + terms[term_id].cursor_x) << 1) + 1) = ATTRIB;
			terms[term_id].cursor_x++;
	}

	vga_redraw_cursor(terms[current_term_id].cursor_x, terms[current_term_id].cursor_y);

	if (userkey) {
		remap_vidmap_page(cur_sch_index);
	}

	restore_flags(flags);
	/* Critical section ends */
}

/* ------------ Private helper functions end ------------ */

/* (The following 3 text mode cursor functions)
 * Reference: https://wiki.osdev.org/Text_Mode_Cursor */

/* void vga_enable_cursor(uint8_t cursor_start, uint8_t cursor_end)
 *   Inputs: cursor_start - the scanline to start the cursor at
 *           cursor_end - the scanline to end the cursor at
 *   Return Value: none
 *   Function: Enables the text-mode cursor */
void vga_enable_cursor(uint8_t cursor_start, uint8_t cursor_end) {
	outb(0x0A, 0x3D4);						/* Select 0x0A (Cursor Start Register) in 0x3D4 (cmd port) */
	outb((inb(0x3D5) & 0xC0) | cursor_start, 0x3D5);	/* Clear CursorStartReg:5 to enable cursor */
	outb(0x0B, 0x3D4);						/* Select 0x0B (Cursor End Register) in 0x3D4 (cmd port) */
	outb((inb(0x3D5) & 0xE0) | cursor_end, 0x3D5);		/* Cursor Skew = -1 */
}

/* void vga_disable_cursor()
 *   Inputs: none
 *   Return Value: none
 *   Function: Disables the text-mode cursor */
void vga_disable_cursor() {
	outb(0x0A, 0x3D4);						/* Select 0x0A (Cursor Start Register) */
	outb(0x20, 0x3D5);						/* Set CursorStartReg:5 to disable cursor */
}

/* void vga_redraw_cursor(int x, int y)
 *   Inputs: x - the x position of the cursor
 *           y - the y position of the cursor
 *   Return Value: none
 *   Function: Updates the text-mode cursor */
void vga_redraw_cursor(int x, int y) {
	uint16_t pos = y * NUM_COLS + x;		/* Calculate position */
	outb(0x0F, 0x3D4);						/* Lower byte -> 0x0F (Cursor Location Low Register) */
	outb((uint8_t) (pos & 0xFF), 0x3D5);
	outb(0x0E, 0x3D4);						 /* Higher byte -> 0x0E (Cursor Location High Register) */
	outb((uint8_t) ((pos >> 8) & 0xFF), 0x3D5);
}

/* void clear(void);
 * Inputs: void
 * Return Value: none
 * Function: Clears video memory;
 *           Set the cursor to the
 *           left top corner of the screen.
 */
void clear(void) {
	int32_t i;
	remap_vidmap_page(current_term_id);
	terms[current_term_id].cursor_x = terms[current_term_id].cursor_y = 0;
	remap_vidmap_page(current_term_id);
	for (i = 0; i < NUM_ROWS * NUM_COLS; i++) {
		*(uint8_t *)(video_mem + (i << 1)) = ' ';
		*(uint8_t *)(video_mem + (i << 1) + 1) = ATTRIB;
	}
	remap_vidmap_page(cur_sch_index);
	vga_redraw_cursor(terms[current_term_id].cursor_x, terms[current_term_id].cursor_y);
}

/* void scroll(void)
 * Inputs: userkey -- indicate if called by user keystroke
 * Return Value: none
 * Function: Scrolls the screen up one line;
 *           Moves the cursor up one line.
 *           (if the cursor is not on the top line)
 */
void scroll(int8_t userkey) {
	int term_id, i;
	switch(userkey) {
		case PUTC_USRKEY: term_id = current_term_id; break;
		case PUTC_PROG: term_id = cur_sch_index; break;
		default: return;
	}

	for (i = 0; i < NUM_ROWS - 1; i++) {
		memcpy((uint8_t *)(video_mem + (i * NUM_COLS * 2)),
			   (uint8_t *)(video_mem + ((i + 1) * NUM_COLS * 2)),
			   NUM_COLS * 2);
	}

	/* Clear the last line */
	for (i = 0; i < NUM_COLS; i++) {
		*(uint8_t *)(video_mem + ((NUM_ROWS - 1) * NUM_COLS * 2) + (i << 1)) = ' ';
		*(uint8_t *)(video_mem + ((NUM_ROWS - 1) * NUM_COLS * 2) + (i << 1) + 1)
			= ATTRIB;
	}

	/* Move the cursor up one line,
	 * but don't move it off the screen */
	if (--terms[term_id].cursor_y == 255) {
		++terms[term_id].cursor_y;
	}
}

/* handle_backspace
 * Inputs: userkey -- indicate if called by user keystroke
 * Return Value: none
 * Function: Handles backspace key press
 */
void handle_backspace(int8_t userkey) {
	int term_id;
	switch(userkey) {
		case PUTC_USRKEY: term_id = current_term_id; break;
		case PUTC_PROG: term_id = cur_sch_index; break;
		default: return;
	}

	if (terms[term_id].cursor_x == 0 && terms[term_id].cursor_y == 0) {
		return;
	}
	terms[term_id].cursor_x--;
	if (terms[term_id].cursor_x == 255) {
		terms[term_id].cursor_y--;
		terms[term_id].cursor_x = NUM_COLS - 1;
	}
	*(uint8_t *)(video_mem + ((NUM_COLS * terms[term_id].cursor_y + terms[term_id].cursor_x) << 1)) = ' ';
}

/* handle_newline
 * Inputs: userkey -- indicate if called by user keystroke
 * Return Value: none
 * Function: Handles newline events
 */
void handle_newline(int8_t userkey) {
	int term_id;
	switch(userkey) {
		case PUTC_USRKEY: term_id = current_term_id; break;
		case PUTC_PROG: term_id = cur_sch_index; break;
		default: return;
	}

	terms[term_id].cursor_x = 0;
	terms[term_id].cursor_y++;
	if (terms[term_id].cursor_y >= NUM_ROWS) {
		scroll(userkey);
	}

	vga_redraw_cursor(terms[current_term_id].cursor_x, terms[current_term_id].cursor_y);
}


/* Standard printf().
 * Only supports the following format strings:
 * %%  - print a literal '%' character
 * %x  - print a number in hexadecimal
 * %u  - print a number as an unsigned integer
 * %d  - print a number as a signed integer
 * %c  - print a character
 * %s  - print a string
 * %#x - print a number in 32-bit aligned hexadecimal, i.e.
 *       print 8 hexadecimal digits, zero-padded on the left.
 *       For example, the hex number "E" would be printed as
 *       "0000000E".
 *       Note: This is slightly different than the libc specification
 *       for the "#" modifier (this implementation doesn't add a "0x" at
 *       the beginning), but I think it's more flexible this way.
 *       Also note: %x is the only conversion specifier that can use
 *       the "#" modifier to alter output. */
int32_t printf(int8_t *format, ...) {

	/* Pointer to the format string */
	int8_t* buf = format;

	/* Stack pointer for the other parameters */
	int32_t* esp = (void *)&format;
	esp++;

	while (*buf != '\0') {
		switch (*buf) {
			case '%':
				{
					int32_t alternate = 0;
					buf++;

format_char_switch:
					/* Conversion specifiers */
					switch (*buf) {
						/* Print a literal '%' character */
						case '%':
							putc('%');
							break;

						/* Use alternate formatting */
						case '#':
							alternate = 1;
							buf++;
							/* Yes, I know gotos are bad.  This is the
							 * most elegant and general way to do this,
							 * IMHO. */
							goto format_char_switch;

						/* Print a number in hexadecimal form */
						case 'x':
							{
								int8_t conv_buf[64];
								if (alternate == 0) {
									itoa(*((uint32_t *)esp), conv_buf, 16);
									puts(conv_buf);
								} else {
									int32_t starting_index;
									int32_t i;
									itoa(*((uint32_t *)esp), &conv_buf[8], 16);
									i = starting_index = strlen(&conv_buf[8]);
									while(i < 8) {
										conv_buf[i] = '0';
										i++;
									}
									puts(&conv_buf[starting_index]);
								}
								esp++;
							}
							break;

						/* Print a number in unsigned int form */
						case 'u':
							{
								int8_t conv_buf[36];
								itoa(*((uint32_t *)esp), conv_buf, 10);
								puts(conv_buf);
								esp++;
							}
							break;

						/* Print a number in signed int form */
						case 'd':
							{
								int8_t conv_buf[36];
								int32_t value = *((int32_t *)esp);
								if(value < 0) {
									conv_buf[0] = '-';
									itoa(-value, &conv_buf[1], 10);
								} else {
									itoa(value, conv_buf, 10);
								}
								puts(conv_buf);
								esp++;
							}
							break;

						/* Print a single character */
						case 'c':
							putc((uint8_t) *((int32_t *)esp));
							esp++;
							break;

						/* Print a NULL-terminated string */
						case 's':
							puts(*((int8_t **)esp));
							esp++;
							break;

						default:
							break;
					}

				}
				break;

			default:
				putc(*buf);
				break;
		}
		buf++;
	}
	return (buf - format);
}

/* int32_t puts(int8_t* s);
 *   Inputs: int_8* s = pointer to a string of characters
 *   Return Value: Number of bytes written
 *    Function: Output a string to the console */
int32_t puts(int8_t* s) {
	register int32_t index = 0;
	while (s[index] != '\0') {
		putc(s[index]);
		index++;
	}
	return index;
}

/* void putc(uint8_t c);
 * Inputs: uint_8* c = character to print
 * Return Value: void
 *  Function: Output a character to the console */
void putc(uint8_t c) {
	__putc(c, PUTC_PROG);
}

/* void putc_userkey(uint8_t c);
 * Inputs: uint_8* c = character to print
 * Return Value: void
 * Function: Forcefully output a character to video memory */
void putc_userkey(uint8_t c) {
	__putc(c, PUTC_USRKEY);
}

/* int8_t* itoa(uint32_t value, int8_t* buf, int32_t radix);
 * Inputs: uint32_t value = number to convert
 *            int8_t* buf = allocated buffer to place string in
 *          int32_t radix = base system. hex, oct, dec, etc.
 * Return Value: number of bytes written
 * Function: Convert a number to its ASCII representation, with base "radix" */
int8_t* itoa(uint32_t value, int8_t* buf, int32_t radix) {
	static int8_t lookup[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	int8_t *newbuf = buf;
	int32_t i;
	uint32_t newval = value;

	/* Special case for zero */
	if (value == 0) {
		buf[0] = '0';
		buf[1] = '\0';
		return buf;
	}

	/* Go through the number one place value at a time, and add the
	 * correct digit to "newbuf".  We actually add characters to the
	 * ASCII string from lowest place value to highest, which is the
	 * opposite of how the number should be printed.  We'll reverse the
	 * characters later. */
	while (newval > 0) {
		i = newval % radix;
		*newbuf = lookup[i];
		newbuf++;
		newval /= radix;
	}

	/* Add a terminating NULL */
	*newbuf = '\0';

	/* Reverse the string and return */
	return strrev(buf);
}

/* int8_t* strrev(int8_t* s);
 * Inputs: int8_t* s = string to reverse
 * Return Value: reversed string
 * Function: reverses a string s */
int8_t* strrev(int8_t* s) {
	register int8_t tmp;
	register int32_t beg = 0;
	register int32_t end = strlen(s) - 1;

	while (beg < end) {
		tmp = s[end];
		s[end] = s[beg];
		s[beg] = tmp;
		beg++;
		end--;
	}
	return s;
}

/* uint32_t strlen(const int8_t* s);
 * Inputs: const int8_t* s = string to take length of
 * Return Value: length of string s
 * Function: return length of string s */
uint32_t strlen(const int8_t* s) {
	register uint32_t len = 0;
	while (s[len] != '\0')
		len++;
	return len;
}

/* void* memset(void* s, int32_t c, uint32_t n);
 * Inputs:    void* s = pointer to memory
 *          int32_t c = value to set memory to
 *         uint32_t n = number of bytes to set
 * Return Value: new string
 * Function: set n consecutive bytes of pointer s to value c */
void* memset(void* s, int32_t c, uint32_t n) {
	c &= 0xFF;
	asm volatile ("                 \n\
			.memset_top:            \n\
			testl   %%ecx, %%ecx    \n\
			jz      .memset_done    \n\
			testl   $0x3, %%edi     \n\
			jz      .memset_aligned \n\
			movb    %%al, (%%edi)   \n\
			addl    $1, %%edi       \n\
			subl    $1, %%ecx       \n\
			jmp     .memset_top     \n\
			.memset_aligned:        \n\
			movw    %%ds, %%dx      \n\
			movw    %%dx, %%es      \n\
			movl    %%ecx, %%edx    \n\
			shrl    $2, %%ecx       \n\
			andl    $0x3, %%edx     \n\
			cld                     \n\
			rep     stosl           \n\
			.memset_bottom:         \n\
			testl   %%edx, %%edx    \n\
			jz      .memset_done    \n\
			movb    %%al, (%%edi)   \n\
			addl    $1, %%edi       \n\
			subl    $1, %%edx       \n\
			jmp     .memset_bottom  \n\
			.memset_done:           \n\
			"
			:
			: "a"(c << 24 | c << 16 | c << 8 | c), "D"(s), "c"(n)
			: "edx", "memory", "cc"
	);
	return s;
}

/* void* memset_word(void* s, int32_t c, uint32_t n);
 * Description: Optimized memset_word
 * Inputs:    void* s = pointer to memory
 *          int32_t c = value to set memory to
 *         uint32_t n = number of bytes to set
 * Return Value: new string
 * Function: set lower 16 bits of n consecutive memory locations of pointer s to value c */
void* memset_word(void* s, int32_t c, uint32_t n) {
	asm volatile ("                 \n\
			movw    %%ds, %%dx      \n\
			movw    %%dx, %%es      \n\
			cld                     \n\
			rep     stosw           \n\
			"
			:
			: "a"(c), "D"(s), "c"(n)
			: "edx", "memory", "cc"
	);
	return s;
}

/* void* memset_dword(void* s, int32_t c, uint32_t n);
 * Inputs:    void* s = pointer to memory
 *          int32_t c = value to set memory to
 *         uint32_t n = number of bytes to set
 * Return Value: new string
 * Function: set n consecutive memory locations of pointer s to value c */
void* memset_dword(void* s, int32_t c, uint32_t n) {
	asm volatile ("                 \n\
			movw    %%ds, %%dx      \n\
			movw    %%dx, %%es      \n\
			cld                     \n\
			rep     stosl           \n\
			"
			:
			: "a"(c), "D"(s), "c"(n)
			: "edx", "memory", "cc"
	);
	return s;
}

/* void* memcpy(void* dest, const void* src, uint32_t n);
 * Inputs:      void* dest = destination of copy
 *         const void* src = source of copy
 *              uint32_t n = number of byets to copy
 * Return Value: pointer to dest
 * Function: copy n bytes of src to dest */
void* memcpy(void* dest, const void* src, uint32_t n) {
	asm volatile ("                 \n\
			.memcpy_top:            \n\
			testl   %%ecx, %%ecx    \n\
			jz      .memcpy_done    \n\
			testl   $0x3, %%edi     \n\
			jz      .memcpy_aligned \n\
			movb    (%%esi), %%al   \n\
			movb    %%al, (%%edi)   \n\
			addl    $1, %%edi       \n\
			addl    $1, %%esi       \n\
			subl    $1, %%ecx       \n\
			jmp     .memcpy_top     \n\
			.memcpy_aligned:        \n\
			movw    %%ds, %%dx      \n\
			movw    %%dx, %%es      \n\
			movl    %%ecx, %%edx    \n\
			shrl    $2, %%ecx       \n\
			andl    $0x3, %%edx     \n\
			cld                     \n\
			rep     movsl           \n\
			.memcpy_bottom:         \n\
			testl   %%edx, %%edx    \n\
			jz      .memcpy_done    \n\
			movb    (%%esi), %%al   \n\
			movb    %%al, (%%edi)   \n\
			addl    $1, %%edi       \n\
			addl    $1, %%esi       \n\
			subl    $1, %%edx       \n\
			jmp     .memcpy_bottom  \n\
			.memcpy_done:           \n\
			"
			:
			: "S"(src), "D"(dest), "c"(n)
			: "eax", "edx", "memory", "cc"
	);
	return dest;
}

/* void* memmove(void* dest, const void* src, uint32_t n);
 * Description: Optimized memmove (used for overlapping memory areas)
 * Inputs:      void* dest = destination of move
 *         const void* src = source of move
 *              uint32_t n = number of byets to move
 * Return Value: pointer to dest
 * Function: move n bytes of src to dest */
void* memmove(void* dest, const void* src, uint32_t n) {
	asm volatile ("                             \n\
			movw    %%ds, %%dx                  \n\
			movw    %%dx, %%es                  \n\
			cld                                 \n\
			cmp     %%edi, %%esi                \n\
			jae     .memmove_go                 \n\
			leal    -1(%%esi, %%ecx), %%esi     \n\
			leal    -1(%%edi, %%ecx), %%edi     \n\
			std                                 \n\
			.memmove_go:                        \n\
			rep     movsb                       \n\
			"
			:
			: "D"(dest), "S"(src), "c"(n)
			: "edx", "memory", "cc"
	);
	return dest;
}

/* int32_t strncmp(const int8_t* s1, const int8_t* s2, uint32_t n)
 * Inputs: const int8_t* s1 = first string to compare
 *         const int8_t* s2 = second string to compare
 *               uint32_t n = number of bytes to compare
 * Return Value: A zero value indicates that the characters compared
 *               in both strings form the same string.
 *               A value greater than zero indicates that the first
 *               character that does not match has a greater value
 *               in str1 than in str2; And a value less than zero
 *               indicates the opposite.
 * Function: compares string 1 and string 2 for equality */
int32_t strncmp(const int8_t* s1, const int8_t* s2, uint32_t n) {
	int32_t i;
	for (i = 0; i < n; i++) {
		if ((s1[i] != s2[i]) || (s1[i] == '\0') /* || s2[i] == '\0' */) {

			/* The s2[i] == '\0' is unnecessary because of the short-circuit
			 * semantics of 'if' expressions in C.  If the first expression
			 * (s1[i] != s2[i]) evaluates to false, that is, if s1[i] ==
			 * s2[i], then we only need to test either s1[i] or s2[i] for
			 * '\0', since we know they are equal. */
			return s1[i] - s2[i];
		}
	}
	return 0;
}

/* int8_t* strcpy(int8_t* dest, const int8_t* src)
 * Inputs:      int8_t* dest = destination string of copy
 *         const int8_t* src = source string of copy
 * Return Value: pointer to dest
 * Function: copy the source string into the destination string */
int8_t* strcpy(int8_t* dest, const int8_t* src) {
	int32_t i = 0;
	while (src[i] != '\0') {
		dest[i] = src[i];
		i++;
	}
	dest[i] = '\0';
	return dest;
}

/* int8_t* strcpy(int8_t* dest, const int8_t* src, uint32_t n)
 * Inputs:      int8_t* dest = destination string of copy
 *         const int8_t* src = source string of copy
 *                uint32_t n = number of bytes to copy
 * Return Value: pointer to dest
 * Function: copy n bytes of the source string into the destination string */
int8_t* strncpy(int8_t* dest, const int8_t* src, uint32_t n) {
	int32_t i = 0;
	while (src[i] != '\0' && i < n) {
		dest[i] = src[i];
		i++;
	}
	while (i < n) {
		dest[i] = '\0';
		i++;
	}
	return dest;
}

/* void sleep(uint32_t ticks)
 * NOTE: ticks = seconds * freq
 *       Since the virtualized "freq" in RTC is not
 *       a global variable, we cannot pass in the 
 *       number of seconds as argument and thus
 *       turn to the number of ticks.
 * Inputs: uint32_t ticks = number of ticks to sleep
 * Return Value: none
 * Function: sleep for a given number of ticks */
void sleep(uint32_t ticks) {
	int32_t i;
	for (i = 0; i < ticks; i++) {
		rtc_read(0, NULL, 0);
	}
}

/* void test_interrupts(void)
 * Inputs: void
 * Return Value: void
 * Function: increments video memory. To be used to test rtc */
void test_interrupts(void) {
	int32_t i;
	for (i = 0; i < NUM_ROWS * NUM_COLS; i++) {
		video_mem[i << 1]++;
	}
}
