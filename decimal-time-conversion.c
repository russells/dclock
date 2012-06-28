#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

static char *myname;

static void usage(int retcode)
{
	fprintf(stderr, "Usage: %s [time]\n"
		"  Time can be in decimal (xx.xx.xx) or normal (xx:xx:xx)\n"
		"  Prints the time in the opposite system\n"
		"  With no time, convert the current time to decimal.\n",
		myname);
	exit(retcode);
}


static void printNormalToDecimal(uint8_t h, uint8_t m, uint8_t s)
{
	uint32_t seconds;
	uint32_t dSeconds;
	uint8_t dh;
	uint8_t dm;
	uint8_t ds;

	seconds = h * 3600 + m * 60 + s;
	dSeconds = (uint32_t) (((float)seconds) * 125.0f / 108.0f);
	ds = dSeconds % 100;
	dSeconds /= 100;
	dm = dSeconds % 100;
	dSeconds /= 100;
	dh = dSeconds % 10;

	printf("%02u.%02u.%02u\n", dh, dm, ds);
}


static void normalToDecimal(const char *sn)
{
	uint8_t h;
	uint8_t m;
	uint8_t s;

	if (sn[0] < '0' || sn[0] > '2'
	    || sn[1] < '0' || sn[1] > '9'
	    || sn[3] < '0' || sn[3] > '5'
	    || sn[4] < '0' || sn[4] > '9'
	    || sn[6] < '0' || sn[6] > '5'
	    || sn[7] < '0' || sn[7] > '9')
		usage(2);
	h = (10 * (sn[0]-'0')) + sn[1] - '0';
	if (h >= 24) {
		fprintf(stderr, "%s: real hour > 23 (%u)\n", myname,  h);
		exit(5);
	}
	m = (10 * (sn[3]-'0')) + sn[4] - '0';
	s = (10 * (sn[6]-'0')) + sn[7] - '0';

	printNormalToDecimal(h, m, s);
}


static void nowToDecimal(void)
{
	struct tm *tm;
	time_t timet;

	timet = time(0);
	tm = localtime(&timet);
	printNormalToDecimal((uint8_t)(tm->tm_hour),
			     (uint8_t)(tm->tm_min),
			     (uint8_t)(tm->tm_sec));
}


static void decimalToNormal(const char *sn)
{
	uint8_t dh;
	uint8_t dm;
	uint8_t ds;
	uint32_t dSeconds;
	uint32_t seconds;
	uint8_t h;
	uint8_t m;
	uint8_t s;

	if (sn[0] < '0' || sn[0] > '1'
	    || sn[1] < '0' || sn[1] > '9'
	    || sn[3] < '0' || sn[3] > '9'
	    || sn[4] < '0' || sn[4] > '9'
	    || sn[6] < '0' || sn[6] > '9'
	    || sn[7] < '0' || sn[7] > '9')
		usage(2);
	dh = (10 * (sn[0]-'0')) + sn[1] - '0';
	if (dh > 10) {
		fprintf(stderr, "%s: decimal hour too big (%u)\n",
			myname, dh);
		exit(5);
	}
	if (10 == dh) {
		dh = 0;
	}
	dm = (10 * (sn[3]-'0')) + sn[4] - '0';
	ds = (10 * (sn[6]-'0')) + sn[7] - '0';
	dSeconds = dh * 10000 + dm * 100 + ds;
	if (dSeconds >= 100000) {
		fprintf(stderr, "%s: total decimal seconds > 100000 (%u)"
			" How?\n",
			myname, dSeconds);
		exit(5);
	}
	seconds = (uint32_t) (((float)dSeconds) * 108.0f / 125.0f);
	if (seconds >= 86400) {
		fprintf(stderr, "%s: real seconds > 86400 (%u) How?\n",
			myname, seconds);
		exit(5);
	}
	s = seconds % 60;
	seconds /= 60;
	m = seconds % 60;
	seconds /= 60;
	h = seconds % 24;
	printf("%02u:%02u:%02u\n", h, m, s);
}


int main(int argc, char **argv)
{
	myname = argv[0];
	if (argc == 1)
		nowToDecimal();
	else if (argc != 2)
		usage(1);
	else if (strlen(argv[1]) != 8)
		usage(2);
	else if (argv[1][2] == '.' && argv[1][5] == '.')
		decimalToNormal(argv[1]);
	else if (argv[1][2] == ':' && argv[1][5] == ':')
		normalToDecimal(argv[1]);
	else
		usage(2);
	return 0;
}
