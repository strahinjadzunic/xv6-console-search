// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

#include "types.h"
#include "defs.h"
#include "traps.h"
#include "spinlock.h"
#include "file.h"
#include "memlayout.h"
#include "proc.h"
#include "x86.h"
#include "printf.h"

#include <stdarg.h>


//Za nasu altmapu
#define KEY_ALT_F 0xAE
#define KEY_ALT_P 0xDA
#define KEY_ALT_N 0xC7
static int korde[285];

static int tren_poz;
static int duz_reci;
static int pronadjeno; //Ukupan broj pronadjenih rezultata

static void consputc(int);

static int pretrazivanje = 0;
static int panicked = 0;

static ushort snap[80*25];
static int snap_kursor;


static char kljucnaRec[72];
static int duz_kR=0;

static struct {
	struct spinlock lock;
	int locking;
} cons;

// Print to the console. only understands %d, %x, %p, %s.
void
cprintf(char *fmt, ...)
{
	int locking;
	va_list args;

	locking = cons.locking;
	if(locking)
		acquire(&cons.lock);

	if (fmt == 0)
		panic("null fmt");

	va_start(args, fmt);

	fnvprintf(consputc, fmt, args);

	va_end(args);

	if(locking)
		release(&cons.lock);
}

void
panic(char *s)
{
	int i;
	uint pcs[10];

	cli();
	cons.locking = 0;
	// use lapiccpunum so that we can call panic from mycpu()
	cprintf("lapicid %d: panic: ", lapicid());
	cprintf(s);
	cprintf("\n");
	getcallerpcs(&s, pcs);
	for(i=0; i<10; i++)
		cprintf(" %p", pcs[i]);
	panicked = 1; // freeze other CPU
	for(;;)
		;
}

#define BACKSPACE 0x100
#define CRTPORT 0x3d4
static ushort *crt = (ushort*)P2V(0xb8000);  // CGA memory

static void
cgaputc(int c)
{
	int pos;

	// Cursor position: col + 80*row.
	outb(CRTPORT, 14);
	pos = inb(CRTPORT+1) << 8;
	outb(CRTPORT, 15);
	pos |= inb(CRTPORT+1);

	if(c == '\n')
		pos += 80 - pos%80;
	else if(c == BACKSPACE){
		if(pos > 0) --pos;
	} else
		crt[pos++] = (c&0xff) | 0x0700;  // black on white

	if(pos < 0 || pos > 25*80)
		panic("pos under/overflow");

	if((pos/80) >= 24){  // Scroll up.
		memmove(crt, crt+80, sizeof(crt[0])*23*80);
		pos -= 80;
		memset(crt+pos, 0, sizeof(crt[0])*(24*80 - pos));
	}

	outb(CRTPORT, 14);
	outb(CRTPORT+1, pos>>8);
	outb(CRTPORT, 15);
	outb(CRTPORT+1, pos);
	crt[pos] = ' ' | 0x0700;
}

void
consputc(int c)
{
	if(panicked){
		cli();
		for(;;)
			;
	}

	if(c == BACKSPACE){
		uartputc('\b'); uartputc(' '); uartputc('\b');
	} else
		uartputc(c);
	cgaputc(c);
}

#define INPUT_BUF 128
struct {
	char buf[INPUT_BUF];
	uint r;  // Read index
	uint w;  // Write index
	uint e;  // Edit index
} input;

#define C(x)  ((x)-'@')  // Control-x




static char toLower(char c){ // da bi pret. bila case insensitive
	if(c >= 'A' && c <= 'Z') return c+32;
	return c;
}


void napravi_prefiksnu_tabelu(int *lps){
	int duz=0, i=1;
	lps[0]=0;
	while(i < duz_kR){
		if(toLower(kljucnaRec[i]) == toLower(kljucnaRec[duz])){
			lps[i++]=++duz;
		} else {
			if(duz != 0) duz=lps[duz-1];
			else lps[i++]=0;
		}
	}
}

//Radimo KMP pret, cuvamo poz. u kordama
void KMP_pretrazi(){
	int lps[72];
	int i=0, j=0;
	pronadjeno=0;
	duz_reci=duz_kR;

	if(duz_kR == 0) return;

	napravi_prefiksnu_tabelu(lps);

	//prolaz kroz crt, trazimo poklapanja
	while(i < 24*80){
		if(toLower(crt[i] & 0xFF) == toLower(kljucnaRec[j])){
			i++; j++;
		}
		if(j == duz_kR){
			
			if(pronadjeno < 285)
				korde[pronadjeno++] = i-j;
			j = lps[j-1];
		} else if(i < 24*80 && toLower(crt[i] & 0xFF) != toLower(kljucnaRec[j])){
			if(j != 0) j=lps[j-1];
			else i++;
		}
	}
}




void istakniRezultate(){
	int i, j;

	//Vracamo redove(boje) u prvobitno stanje
	for(i=0; i < 24*80; i++)
		crt[i] = (snap[i] & 0xFF) | 0xF000;

	if(pronadjeno == 0) return;

	
	if(tren_poz >= pronadjeno) tren_poz=0;

	// ljubicasta pozadina za sva pojavljivanja 
	for(i=0; i < pronadjeno; i++)
		for(j=0; j < duz_reci; j++)
			crt[korde[i]+j] = (crt[korde[i]+j] & 0xFF) | 0x5F00;

	//za tren. selek. zelena pozadina
	for(j=0; j < duz_reci; j++)
		crt[korde[tren_poz]+j] = (crt[korde[tren_poz]+j] & 0xFF) | 0x2F00;

	//ovde prakticno pomeramo kursor na hardverskom nivou
	int poz = korde[tren_poz];
	outb(CRTPORT, 14);
	outb(CRTPORT+1, poz>>8);
	outb(CRTPORT, 15);
	outb(CRTPORT+1, poz);
}

//crtamo search bar sa trenutnim sadrzajem iz kljucnaRec
void crtaj_searchBar(){
	int i;
	
	for(i=0; i < 80; i++) //Cistimo poslednji red
		crt[24*80+i] = ' ' | 0x0000;
	// prompt zelenom
	char *prompt = "Search: ";
	for(i=0; prompt[i]; i++)
		crt[24*80+i] = (prompt[i] & 0xFF) | 0x0A00;
	
	for(i=0; i < duz_kR; i++)//a ovde da nam je uneseni tekst sivom bojom
		crt[24*80+8+i] = (kljucnaRec[i] & 0xFF) | 0x0700;
	//premestamo kursor pored searcha
	crt[24*80 + 8 + duz_kR] = ' ' | 0x0700;  // da bi kursor bio vidljiv
	int poz_kursora = 24*80 + 8 + duz_kR;
	outb(CRTPORT, 14);
	outb(CRTPORT+1, poz_kursora>>8);
	outb(CRTPORT, 15);
	outb(CRTPORT+1, poz_kursora);
}


void okreniBoje(){
	int i;
	//U redovima od 0 do 23 imamo crna slova na beloj pozadini
	for(i = 0; i < 24*80; i++)
		crt[i] = (crt[i] & 0xFF) | 0xF000;

	//Cisti poslednji red(24.)
	for(i = 24*80; i < 25*80; i++)
		crt[i] = ' ' | 0x0000;

	// Ispisuje sadrzaj pod navodnicima zelenom bojom, dok je pozadina crna

	char *prompt = "Search: ";
	for(i = 0; prompt[i]; i++)
		crt[24*80 + i] = (prompt[i] & 0xFF) | 0x0A00;
}

void
consoleintr(int (*getc)(void))
{
	int c, doprocdump = 0;

	acquire(&cons.lock);
	while((c = getc()) >= 0){

		if(c == KEY_ALT_F){
			if(pretrazivanje == 0){
				pretrazivanje = 1;
				//cuvamp ekran i poziciju kursora
				for(int i = 0; i < 25*80; i++)
					snap[i] = crt[i];
				outb(CRTPORT, 14);
				snap_kursor = inb(CRTPORT+1) << 8;
				outb(CRTPORT, 15);
				snap_kursor |= inb(CRTPORT+1);
				okreniBoje();
				crtaj_searchBar();
				//prikazuje preth. pret, ukoliko postoji
				if(duz_kR > 0){
					tren_poz=0;
					KMP_pretrazi();
					istakniRezultate();
				}
			} else {
				pretrazivanje = 0;
				//vracamo sve kako je bilo
				for(int i = 0; i < 25*80; i++)
					crt[i] = snap[i];
				outb(CRTPORT, 14);
				outb(CRTPORT+1, snap_kursor>>8);
				outb(CRTPORT, 15);
				outb(CRTPORT+1, snap_kursor);
			}
			continue;
		}

		if(pretrazivanje == 1){
			if(c == KEY_ALT_N){

				if(pronadjeno > 0){
					tren_poz = (tren_poz+1) % pronadjeno;
					istakniRezultate();
				}
			} else if(c == KEY_ALT_P){
				if(pronadjeno > 0){
					tren_poz = (tren_poz-1+pronadjeno) % pronadjeno;
					istakniRezultate();
				}
			} else if(c == C('H') || c == '\x7f'){//backspace sluc, brisemo karak. iz kljucnaRec
				if(duz_kR > 0){
					kljucnaRec[--duz_kR] = 0;
					tren_poz=0;
					crtaj_searchBar();
					KMP_pretrazi();
					istakniRezultate();
				}
			} else if(c >= ' ' && c < 0x80){ //unos karaktera u polje za pretragu
			
				if(duz_kR < 71){
					kljucnaRec[duz_kR++] = c;
					kljucnaRec[duz_kR] = 0;
					tren_poz=0;
					crtaj_searchBar();
					KMP_pretrazi();
					istakniRezultate();
				}
			}
			
			continue;
		}

		switch(c){
		case C('P'):  // Process listing.
			// procdump() locks cons.lock indirectly; invoke later
			doprocdump = 1;
			break;
		case C('U'):  // Kill line.
			while(input.e != input.w &&
			      input.buf[(input.e-1) % INPUT_BUF] != '\n'){
				input.e--;
				consputc(BACKSPACE);
			}
			break;
		case C('H'): case '\x7f':  // Backspace
			if(input.e != input.w){
				input.e--;
				consputc(BACKSPACE);
			}
			break;
		default:
			if(c != 0 && input.e-input.r < INPUT_BUF){
				c = (c == '\r') ? '\n' : c;
				input.buf[input.e++ % INPUT_BUF] = c;
				consputc(c);
				if(c == '\n' || c == C('D') || input.e == input.r+INPUT_BUF){
					input.w = input.e;
					wakeup(&input.r);
				}
			}
			break;
		}
	}
	release(&cons.lock);
	if(doprocdump) {
		procdump();  // now call procdump() wo. cons.lock held
	}
}

int
consoleread(struct inode *ip, char *dst, int n)
{
	uint target;
	int c;

	iunlock(ip);
	target = n;
	acquire(&cons.lock);
	while(n > 0){
		while(input.r == input.w){
			if(myproc()->killed){
				release(&cons.lock);
				ilock(ip);
				return -1;
			}
			sleep(&input.r, &cons.lock);
		}
		c = input.buf[input.r++ % INPUT_BUF];
		if(c == C('D')){  // EOF
			if(n < target){
				// Save ^D for next time, to make sure
				// caller gets a 0-byte result.
				input.r--;
			}
			break;
		}
		*dst++ = c;
		--n;
		if(c == '\n')
			break;
	}
	release(&cons.lock);
	ilock(ip);

	return target - n;
}

int
consolewrite(struct inode *ip, char *buf, int n)
{
	int i;

	iunlock(ip);
	acquire(&cons.lock);
	for(i = 0; i < n; i++)
		consputc(buf[i] & 0xff);
	release(&cons.lock);
	ilock(ip);

	return n;
}

void
consoleinit(void)
{
	initlock(&cons.lock, "console");

	devsw[CONSOLE].write = consolewrite;
	devsw[CONSOLE].read = consoleread;
	cons.locking = 1;

	ioapicenable(IRQ_KBD, 0);
}
