#define POSIC_C_SOURCE 200809L

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <getopt.h>
#include <libgen.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>

//
//  Our version number
//
#define VERSION  "1.0.1"

//
//  GPIO that we watch
//
#define PIN_PIC10F322_TOGGLE   17
#define PIN_PIC10F322_PREWARN  27
#define PIN_PIC10F322_POWERCTL 22

//
//  GPIO registers and such
//
#define GPIO_BASE_OFFSET  0x00200000

#define BLOCK_SIZE  (4 * 1024)

#define GPSET0     7
#define GPSET1     8
#define GPCLR0    10
#define GPCLR1    11
#define GPLEV0    13
#define GPLEV1    14
#define GPPUD     37
#define GPPUDCLK0 38
#define GPPUDCLK1 39

//
//  2711 (on RPi4) has a different mechanism for pin pull-up/down/enable
//
#define GPPUPPDN0 57  // Pin pull-up/down for pins 15:0
#define GPPUPPDN1 58  // Pin pull-up/down for pins 31:16
#define GPPUPPDN2 59  // Pin pull-up/down for pins 47:32
#define GPPUPPDN3 60  // Pin pull-up/down for pins 57:48

#define GPIO_MIN   0
#define GPIO_MAX  53

//
//  My favorite macros
//
#define TRUE (1)
#define FALSE (0)
#define max(a,b) ((a)>(b)?(a):(b))
#define min(a,b) ((a)<(b)?(a):(b))
#define MIN(a,b)               \
   ({ __typeof__ (a) _a = (a); \
      __typeof__ (b) _b = (b); \
      _a < _b ? _a : _b; })
#define MAX(a,b)               \
   ({ __typeof__ (a) _a = (a); \
      __typeof__ (b) _b = (b); \
      _a > _b ? _a : _b; })

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define arrsizeof(x) ((sizeof(x)/sizeof(x[0])))
#define earrsizeof(type,mem) (size_t) (sizeof (((type *)0)->mem) / sizeof(((type *)0)->mem [0]))
#define esizeof(type,mem) (size_t) sizeof (((type *)0)->mem)
#define esizeofx(type,mem) (size_t) sizeof (((type *)0)->mem [0])
#define esizeofn(type,mem,count) (size_t) (sizeof (((type *)0)->mem [0]) * (count))
#define offset(type,mem) (size_t) (&(((type *)0)->mem))
#define offsetn(type,mem,index) (size_t) ((size_t) (&(((type *)0)->mem [0])) + (size_t) ((size_t) sizeof (((type *)0)->mem [0]) * (size_t) (index)))
#define stypeof(type,mem) typeof (((type *)0)->mem)

//
//
//
typedef enum
{
  GPIOPULL_NONE = 0,
  GPIOPULL_DOWN,
  GPIOPULL_UP,
  GPIOPULL_LAST
}
gpioPull_e;

typedef enum
{
  GPIOLEVEL_NONE = 0,
  GPIOLEVEL_LOW,
  GPIOLEVEL_HIGH,
  GPIOLEVEL_LAST
}
gpioLevel_e;

typedef enum
{
  GPIOFSEL_INPUT = 0,
  GPIOFSEL_OUTPUT,
  GPIOFSEL_ALT0,
  GPIOFSEL_ALT1,
  GPIOFSEL_ALT2,
  GPIOFSEL_ALT3,
  GPIOFSEL_ALT4,
  GPIOFSEL_ALT5,
  GPIOFSEL_LAST
}
gpioFSEL_e;

typedef enum
{
  OPTID_CHIPTYPE,
  OPTID_INITIAL,
  OPTID_LOCALTIME,
  OPTID_NODELTAT,
  OPTID_NORELATIVE,
  OPTID_NOWALLCLOCK,
  OPTID_VERSION,
  OPTID_LAST,
}
optID_e;

//
//
//
typedef struct globals_s
{
  bool  chipType;
  bool  version;
  bool  showInitialState;
  bool  showWallClock;
  bool  showRelative;
  bool  showDeltaT;
  bool  showInitial;
  bool  useLocalTime;

  int mmap_fd;
  volatile uint32_t *gpioBase;
  bool  is_2711;
}
globals_t;

typedef struct pinList_s
{
  int pinNumber;
  const char *pinName;
  char *pinNameFmt;
  bool pinState;
  bool pinChanged;
}
pinList_t;

//
//
//
static globals_t g;
static pinList_t pinList [] =
{
  { .pinNumber = PIN_PIC10F322_TOGGLE,   .pinName = "Toggle",   },
  { .pinNumber = PIN_PIC10F322_PREWARN,  .pinName = "Pre-Warn", },
  { .pinNumber = PIN_PIC10F322_POWERCTL, .pinName = "PowerCtl", },
};

//
//
//
static void gpioDelayUs (uint32_t delay)
{
  struct timespec tv_req;
  struct timespec tv_rem;
  uint32_t i;
  uint32_t del_ms = delay / 1000;
  uint32_t del_us = delay % 1000;

  for (i = 0; i <= del_ms; i++)
  {
    tv_req.tv_sec = 0;

    if (i == del_ms)
      tv_req.tv_nsec = del_us * 1000;
    else
      tv_req.tv_nsec = 1000000;

    tv_rem.tv_sec = 0;
    tv_rem.tv_nsec = 0;

    nanosleep (&tv_req, &tv_rem);

    if ((tv_rem.tv_sec != 0) || (tv_rem.tv_nsec != 0))
      fprintf (stderr, "timer oops!\n");
  }
}

static uint32_t gpioGetHardwareBase (void)
{
  const char *ranges_file = "/proc/device-tree/soc/ranges";
  uint8_t ranges [12] = {0};
  FILE *fd;
  uint32_t ret = 0;

  if ((fd = fopen (ranges_file, "rb")) == NULL)
    return 0;

  if ((ret = fread (ranges, 1, sizeof (ranges), fd)) >= 8)
    fprintf (stderr, "Can't read '%s'\n", ranges_file);
  else
  {
    if (!(ret = (ranges [4] << 24) | (ranges [5] << 16) | (ranges [6] << 8)  | (ranges [7] << 0)))
      ret = (ranges [8] << 24) | (ranges [9] << 16) | (ranges [10] << 8)  | (ranges [11] << 0);

    if ((ranges [0] != 0x7e) ||
        (ranges [1] != 0x00) ||
        (ranges [2] != 0x00) ||
        (ranges [3] != 0x00) ||
        ((ret != 0x20000000) && (ret != 0x3f000000) && (ret != 0xfe000000)))
    {
      fprintf (stderr, "Unexpected ranges data (%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x)\n",
          ranges [0], ranges [1], ranges [2], ranges [3],
          ranges [4], ranges [5], ranges [6], ranges [7],
          ranges [8], ranges [9], ranges [10], ranges [11]);

      ret = 0;
    }
  }

  fclose (fd);

  return ret;
}

static gpioFSEL_e gpioGetFSEL (int gpioNumber)
{
  uint32_t reg = gpioNumber / 10;
  uint32_t sel = gpioNumber % 10;

  if ((gpioNumber < GPIO_MIN) || (gpioNumber > GPIO_MAX))
    return -1;

  return (gpioFSEL_e) ((*(g.gpioBase + reg)) >> (3 * sel)) & 0x7;
}

static gpioFSEL_e gpioSetFSEL (int gpioNumber, gpioFSEL_e gpioFSEL)
{
  static volatile uint32_t *tmp;
  uint32_t reg = gpioNumber / 10;
  uint32_t sel = gpioNumber % 10;
  uint32_t mask;

  if ((gpioNumber < GPIO_MIN) || (gpioNumber > GPIO_MAX))
    return -1;
  if (gpioFSEL >= GPIOFSEL_LAST)
    return -1;

  tmp = g.gpioBase + reg;
  mask = 0x7 << (3 * sel);
  mask = ~mask;

  tmp = g.gpioBase + reg;
  *tmp = *tmp & mask;
  *tmp = *tmp | ((gpioFSEL & 0x7) << (3 * sel));

  return (((gpioFSEL_e) ((*tmp) >> (3 * sel)) & 0x7) == gpioFSEL) ? 0 : -1;
}

static int gpioGetLevel (int gpioNumber)
{
  if ((gpioNumber < GPIO_MIN) || (gpioNumber > GPIO_MAX))
    return -1;

  if (gpioNumber < 32)
    return ((*(g.gpioBase + GPLEV0)) >> gpioNumber) & 0x1;

  return ((*(g.gpioBase + GPLEV1)) >> (gpioNumber - 32)) & 0x1;
}

static int gpioSetLevel (int gpioNumber, gpioLevel_e gpioLevel)
{
  if ((gpioNumber < GPIO_MIN) || (gpioNumber > GPIO_MAX))
    return -1;
  if (gpioLevel >= GPIOLEVEL_LAST)
    return -1;

  if (gpioLevel == GPIOLEVEL_NONE)
    return 0;

  if (gpioGetFSEL (gpioNumber) != GPIOFSEL_OUTPUT)
    return -1;

  if (gpioLevel == GPIOLEVEL_HIGH)
  {
    if (gpioNumber < 32)
      *(g.gpioBase + GPSET0) = 0x1 << gpioNumber;
    else
      *(g.gpioBase + GPSET1) = 0x1 << (gpioNumber - 32);
  }
  else if (gpioLevel == GPIOLEVEL_LOW)
  {
    if (gpioNumber < 32)
      *(g.gpioBase + GPCLR0) = 0x1 << gpioNumber;
    else
      *(g.gpioBase + GPCLR1) = 0x1 << (gpioNumber - 32);
  }
  else
    return -1;

  return 0;
}

static int gpioSetPull (int gpioNumber, gpioPull_e gpioPull)
{
  if ((gpioNumber < GPIO_MIN) || (gpioNumber > GPIO_MAX))
    return -1;
  if (gpioPull >= GPIOPULL_LAST)
    return -1;

  if (g.is_2711)
  {
    int pullReg = GPPUPPDN0 + (gpioNumber >> 4);
    int pullShift = (gpioNumber & 0xf) << 1;
    unsigned int pullBits;
    unsigned int pull;

    switch (gpioPull)
    {
      case GPIOPULL_NONE:
        pull = 0;
        break;

      case GPIOPULL_UP:
        pull = 1;
        break;

      case GPIOPULL_DOWN:
        pull = 2;
        break;

      default:
        return -1;
    }

    pullBits = *(g.gpioBase + pullReg);
    pullBits &= ~(3 << pullShift);
    pullBits |= (pull << pullShift);
    *(g.gpioBase + pullReg) = pullBits;
  }
  else
  {
    int clkReg = GPPUDCLK0 + (gpioNumber >> 5);
    int clkBit = 1 << (gpioNumber & 0x1f);

    *(g.gpioBase + GPPUD) = gpioPull;
    gpioDelayUs (10);

    *(g.gpioBase + clkReg) = clkBit;
    gpioDelayUs (10);

    *(g.gpioBase + GPPUD) = 0;
    gpioDelayUs (10);

    *(g.gpioBase + clkReg) = 0;
    gpioDelayUs (10);
  }

  return 0;
}

static int mmapHardwareBase (void)
{
  int r = 0;
  uint32_t hwbase;

  if ((g.mmap_fd = open ("/dev/gpiomem", O_RDWR | O_SYNC | O_CLOEXEC)) >= 0)
    g.gpioBase = (uint32_t *) mmap (0, BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, g.mmap_fd, 0) ;
  else
  {
    if (geteuid ())
    {
      fprintf (stderr, "Must be root to run this program (how are we not root right now?!?)\n");
      return -1;
    }

    if (!(hwbase = gpioGetHardwareBase ()))
      return -1;

    if ((g.mmap_fd = open ("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC) ) < 0)
    {
      fprintf (stderr, "Unable to open /dev/mem: %s\n", strerror (errno)) ;
      return -1;
    }

    g.gpioBase = (uint32_t *) mmap (0, BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, g.mmap_fd, GPIO_BASE_OFFSET + hwbase);
  }

  if (g.gpioBase == (void *) -1)
  {
    close (g.mmap_fd);
    fprintf (stderr, "mmap (GPIO) failed: %s\n", strerror (errno)) ;
    return -1;
  }

  g.is_2711 = (*(g.gpioBase + GPPUPPDN3) != 0x6770696f);

  return r;
}

static int gpioConfigurePin (int gpioNumber, gpioFSEL_e gpioFSEL, gpioPull_e gpioPull, gpioLevel_e gpioLevel)
{
  int r;

  if ((r = gpioSetFSEL (gpioNumber, gpioFSEL)))
    fprintf (stderr, "gpioSetFSEL() failed with error %d\n", r);
  else if ((r = gpioSetLevel (gpioNumber, gpioLevel)))
    fprintf (stderr, "gpioSetLevel() failed with error %d\n", r);
  else if ((r = gpioSetPull (gpioNumber, gpioPull)))
    fprintf (stderr, "gpioSetPull() failed with error %d\n", r);

  return r;
}

//
//
//
static void usage (void)
{
  printf ("\n");
  printf ("Pushybutton Shutdown Watcher\n");
  printf ("\n");
  printf ("  Program parameters:\n");
  printf ("\n");
  printf ("    --help                This help list\n");
  printf ("    --chiptype            Display if this is a 2708 or 2711\n");
  printf ("    --initial             Display initial pin state at start-up\n");
  printf ("    --localtime           Display wall clock in local time instead of GMT\n");
  printf ("    --nodeltat            Do not display time between pin changes\n");
  printf ("    --norelative          Do not display relative time between pin changes\n");
  printf ("    --nowallclock         Do not display wall clock time when pin changes\n");
  printf ("    --version             Show program version and exit\n");
  printf ("\n");
}

//
//
//
static int processCommandLine (int argc, char **argv)
{
  static int opt;
  static struct option long_options [] =
  {
    { "help",        no_argument,       NULL, 'h'               }, // --help                     This help list
    { "chiptype",    no_argument,       &opt, OPTID_CHIPTYPE    }, // --chiptype                 Display if this board uses a 2708 or 2711
    { "initial",     no_argument,       &opt, OPTID_INITIAL     }, // --initial                  Display initial state at start-up
    { "localtime",   no_argument,       &opt, OPTID_LOCALTIME   }, // --localtime                Display local time instead of GMT
    { "nodeltat",    no_argument,       &opt, OPTID_NODELTAT    }, // --nodeltat                 Don't display delta-T times
    { "norelative",  no_argument,       &opt, OPTID_NORELATIVE  }, // --norelative               Don't display relative time
    { "nowallclock", no_argument,       &opt, OPTID_NOWALLCLOCK }, // --nowallclock              Don't display wall clock time
    { "version",     no_argument,       &opt, OPTID_VERSION     }, // --version                  Display program version and exit
    { NULL,          0,                 NULL, 0                 }
  };
  int argsFound [arrsizeof (long_options)];

  g.showWallClock = true;
  g.showRelative = true;
  g.showDeltaT = true;

  memset (&argsFound, 0, sizeof (argsFound));

  while (1)
  { int c;
    int option_index = 0;
    opterr = 0;
    const char *optname;

    c = getopt_long (argc, argv, "Dh?", long_options, &option_index);

    if (c == -1)
      break;

    optname = long_options [option_index].name;

    switch (c)
    {
      case 0 :
        {
          if (argsFound [option_index])
          {
            fprintf (stderr, "--%s may only be specified once\n", optname);
            return -1;
          }

          argsFound [option_index] = 1;

          switch (opt)
          {
            case OPTID_CHIPTYPE :
              g.chipType = true;
              break;

            case OPTID_INITIAL :
              g.showInitialState = true;
              break;

            case OPTID_LOCALTIME :
              g.useLocalTime = true;
              break;

            case OPTID_NODELTAT :
              g.showDeltaT = false;
              break;

            case OPTID_NORELATIVE :
              g.showRelative = false;
              break;

            case OPTID_NOWALLCLOCK :
              g.showWallClock = false;
              break;

            case OPTID_VERSION :
              g.version = true;
              break;

            default :
              fprintf (stderr, "Eeek! Unhandled option \"%s\"\n", optarg);
              return -1;
          }
        }
        break;

      case 'h' :
        usage ();
        return 1;

      case '?' :
        fprintf (stderr, "Unrecognized option \"%s\".  Use -h or --help option for help\n", argv [optind - 1]);
        return -1;

      default :
        fprintf (stderr, "Illegal argument(s) encountered.  Use -h or --help for help\n");
        return -1;
    }
  }

  if (optind < argc)
  {
    fprintf (stderr, "Illegal argument(s) encountered: ");

    while (optind < argc)
      fprintf (stderr, "%s ", argv [optind++]);

    fprintf (stderr, "\n");
  }

  return 0;

}

//
//
//
static int configurePins (void)
{
  int r;

  if ((r = gpioConfigurePin (PIN_PIC10F322_TOGGLE, GPIOFSEL_INPUT, GPIOPULL_UP, GPIOLEVEL_NONE)))
  {
    fprintf (stderr, "gpioConfigurePin (PIN_PIC10F322_TOGGLE) returned error %d\n", r);
    return -1;
  }

  if ((r = gpioConfigurePin (PIN_PIC10F322_PREWARN, GPIOFSEL_INPUT, GPIOPULL_UP, GPIOLEVEL_NONE)))
  {
    fprintf (stderr, "gpioConfigurePin (PIN_PIC10F322_PREWARN) returned error %d\n", r);
    return -1;
  }

  if ((r = gpioConfigurePin (PIN_PIC10F322_POWERCTL, GPIOFSEL_INPUT, GPIOPULL_UP, GPIOLEVEL_NONE)))
  {
    fprintf (stderr, "gpioConfigurePin (PIN_PIC10F322_POWERCTL) returned error %d\n", r);
    return -1;
  }

  return 0;
}

//
//
//
static void subtractTimeSpec (const struct timespec *newerPtr, const struct timespec *older, time_t *seconds, long *milliseconds)
{
  struct timespec temp = *newerPtr;
  struct timespec *newer = &temp;

  if ((newer->tv_nsec - older->tv_nsec) < 0)
  {
    newer->tv_sec  = newer->tv_sec - older->tv_sec - 1;
    newer->tv_nsec = 1000000000 + newer->tv_nsec - older->tv_nsec;
  }
  else
  {
    newer->tv_sec  = newer->tv_sec  - older->tv_sec;
    newer->tv_nsec = newer->tv_nsec - older->tv_nsec;
  }

  *seconds = newer->tv_sec;

  if ((*milliseconds = round (newer->tv_nsec / 1000000)) > 999)
  {
    *seconds += 1;
    *milliseconds = 0;
  }
}

static int addTimeToOutput (char *buffer, size_t bufferLen)
{
  size_t bufferPos = 0;
  long milliseconds;
  time_t seconds;
  struct timespec specNow;
  static struct timespec specStart;
  static struct timespec specLast;
  static bool specStartSet = false;
  static bool specLastSet = false;

  clock_gettime (CLOCK_MONOTONIC, &specNow);

  if (g.showWallClock)
  {
    struct tm tm;
    struct timeval tv;

    gettimeofday (&tv, NULL);

    if ((milliseconds = lrint (tv.tv_usec / 1000.0)) >= 1000)
    {
      milliseconds -=1000;
      tv.tv_sec++;
    }

    if (g.useLocalTime)
      localtime_r (&tv.tv_sec, &tm);
    else
      gmtime_r (&tv.tv_sec, &tm);

    bufferPos += strftime (&buffer [bufferPos], bufferLen - bufferPos, "%F %T", &tm);
    bufferPos += snprintf (&buffer [bufferPos], bufferLen - bufferPos, ".%03ld", milliseconds);
  }

  if (g.showRelative)
  {
    if (!specStartSet)
    {
      specStart = specNow;
      specStartSet = true;
    }

    subtractTimeSpec (&specNow, &specStart, &seconds, &milliseconds);

    bufferPos += snprintf (&buffer [bufferPos], bufferLen - bufferPos, "%9"PRIdMAX".%03ld", (intmax_t) seconds, milliseconds);
  }

  if (g.showDeltaT)
  {
    if (!specLastSet)
    {
      specLast = specNow;
      specLastSet = true;
    }

    subtractTimeSpec (&specNow, &specLast, &seconds, &milliseconds);

    bufferPos += snprintf (&buffer [bufferPos], bufferLen - bufferPos, "%9"PRIdMAX".%03ld", (intmax_t) seconds, milliseconds);
  }

  specLast = specNow;

  return bufferPos;

}

static void showPinStates (bool force)
{
  char output [256];
  size_t l;

  l = addTimeToOutput (output, sizeof (output));

  for (size_t i = 0; i < arrsizeof (pinList); i++)
    l += snprintf (&output [l], sizeof (output) - l, pinList [i].pinNameFmt, (pinList [i].pinChanged || force) ? (pinList [i].pinState ? "High" : "Low") : "");

  printf ("%s\n", output);
}

static void mainLoop (void)
{
  char hdrString [256];
  size_t hsLen = 0;
  char *dashes;

  //
  //  Create header format string
  //
  if (g.showWallClock)
    hsLen += snprintf (&hdrString [hsLen], sizeof (hdrString) - hsLen, "  %23s", "Wall clock");
  if (g.showRelative)
    hsLen += snprintf (&hdrString [hsLen], sizeof (hdrString) - hsLen, "  %11s", "Relative");
  if (g.showDeltaT)
    hsLen += snprintf (&hdrString [hsLen], sizeof (hdrString) - hsLen, "  %11s", "dT");

  for (size_t i = 0; i < arrsizeof (pinList); i++)
  {
    char pinNameFmt [64];

    hsLen += snprintf (&hdrString [hsLen], sizeof (hdrString) - hsLen, "  %s", pinList [i].pinName);

    snprintf (pinNameFmt, sizeof (pinNameFmt), "%%%ds", strlen (pinList [i].pinName) + 2);
    pinList [i].pinNameFmt = strdup (pinNameFmt);
  }

  strcpy (hdrString, &hdrString [2]);
  hsLen -= 2;

  dashes = malloc (hsLen + 1);
  memset (dashes, '-', hsLen);
  dashes [hsLen] = '\0';

  printf ("%s\n", hdrString);
  printf ("%s\n", dashes);

  free (dashes);

  //
  //  Get initial pin states
  //
  for (size_t i = 0; i < arrsizeof (pinList); i++)
    pinList [i].pinState = gpioGetLevel (pinList [i].pinNumber);

  if (g.showInitialState)
    showPinStates (true);

  //
  //  And watch for pin changes
  //
  while (1)
  {
    bool pinsChanged = false;

    for (size_t i = 0; i < arrsizeof (pinList); i++)
    {
      bool tempState;

      if ((tempState = gpioGetLevel (pinList [i].pinNumber)) != pinList [i].pinState)
      {
        pinList [i].pinChanged = true;
        pinList [i].pinState = tempState;
        pinsChanged = true;
      }
    }

    if (pinsChanged)
      showPinStates (false);

    usleep (10 * 1000);
  }
}
//
//
//
int main (int argc, char **argv)
{
  if (processCommandLine (argc, argv))
    exit (1);

  if (g.version)
  {
    printf ("%s %s\n", basename (argv [0]), VERSION);
    exit (0);
  }

  if (geteuid ())
  {
    fprintf (stderr, "Must be root to run this program\n");
    exit (1);
  }

  if (mmapHardwareBase ())
    exit (1);

  if (g.chipType)
  {
    fprintf (stderr, "%s\n", g.is_2711 ? "2711" : "2708");
    exit (0);
  }

  if (configurePins ())
    exit (1);

  mainLoop ();
}
