/* =========================================================================
 * Generic camera control tool providing a shell to interact with the camera
 * 
 * writes images to shared memory (ImageStreamIO library by O. Guyon)
 * can write info to a named pipe if required
 * ========================================================================= */

#include <stdio.h>

#ifdef __GNUC__
#  if(__GNUC__ > 3 || __GNUC__ ==3)
#	define _GNUC3_
#  endif
#endif

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <math.h>
#include <pthread.h>
#include <curses.h>

#include "ImageStruct.h"
#include "ImageStreamIO.h"

/* =========================================================================
 *                camera configuration description structure
 * ========================================================================= */
typedef struct {
  float exp_time; // exposure time in seconds
  float acc_time; // accumulate cycle time in seconds (cf SDK)
  float kin_time; // kinetic cycle time in seconds (cf SDK)
  float frate;    // experienced frame rate (Hz)
  // ---------
  int XW;         // image (window) width
  int YW;         // image (window) height
  // ---------
  int nleft;      // number of images left in acquisition
  int nextframe;  // index of the next frame to write
  // ---------
  int bstreamon; // camera streaming? (continuous acq.)
  int bacqon;    // camera acquiring? 
  int babort;    // abort command was issued?
  int bcamOK;    // the happy camera flag!
  // ------------

} cam_config;

/* =========================================================================
 *                            Global variables
 * ========================================================================= */
IMAGE *imarray;    // shared memory img pointer (defined in ImageStreamIO.h)
cam_config *cconf; // camera config pointer

#define LINESIZE 256
char myfifout[LINESIZE] = "/tmp/camshell_fifo_out";
int verbose = 0;
char dashline[80] =
  "-----------------------------------------------------------------------------\n";

/* =========================================================================
 *                  initialize the camconf structure
 * ========================================================================= */
void cam_config_init(cam_config* camconf) {
  camconf->exp_time = 0.00001; // default: shortest exp time
  camconf->acc_time = 0.0;
  camconf->kin_time = 0.0;
  camconf->frate = 0.0;
  
  camconf->bstreamon = 0;
  camconf->bacqon = 0;
  camconf->babort = 0;
  camconf->bcamOK = 1;
}

/* =========================================================================
 *                       Displays the help menu
 * ========================================================================= */
void print_help() {
  char fmt[20] = "%15s %20s %40s\n";
  attron(COLOR_PAIR(3));
  mvprintw(6, 0, dashline);

  //  printw("%s", dashline);
  printw("            camera control shell help menu\n");
  printw("%s", dashline);
  printw(fmt, "command", "parameters", "description");
  printw("%s", dashline);
  printw(fmt, "status", "",        "ready, isbeingcooled, standby, ...");
  printw(fmt, "get_frate", "",     "get camera frame rate (in Hz)");
  printw(fmt, "stream", "",        "start the acquisition (inf. loop)");
  printw(fmt, "abort",  "",        "stop the acquisition");
  printw(fmt, "quit", "",          "stops the camera!");
  printw("%s", dashline);
  attroff(COLOR_PAIR(3));
  //getch();
}

/* =========================================================================
 *                        generic server command
 * ========================================================================= */
int server_command(int ed, const char *cmd) {
  char tmpbuf[LINESIZE];

  sprintf(tmpbuf, "%s\r", cmd);
  if (verbose)
    printf("command: %s, ed = %d", tmpbuf, ed);
  return 0;
}

/* =========================================================================
 *                    generic server query (expects float)
 * ========================================================================= */
float server_query_float(int ed, const char *cmd) {
  char outbuf[2000];
  float fval;

  server_command(ed, cmd);
  usleep(100000);
  //readpdvcli(ed, outbuf);
  sscanf(outbuf, "%f", &fval);

  return fval;
}

/* =========================================================================
 *                        log server interaction
 * ========================================================================= */

void log_action(char *msg) {
  printf("%s\n", msg);
}

/* =========================================================================
 *               return values to client via named pipe
 * ========================================================================= */

void send_to_fifo(char *fifoname, char *msg) {
  int wfd = 0; // file descriptor for output fifo

  wfd = open(fifoname, O_WRONLY | O_NONBLOCK);
  write(wfd, msg, strlen(msg));
  close(wfd);
  wfd = 0;
}

/* =========================================================================
 *                   continuous acquisition thread
 * ========================================================================= */
void* acquire(void *params) {
  cam_config* camconf = (cam_config*) params;

  // ------------------------ frame rate -----------------------
  int nfr = 10, ifr = 0;     // variables used to estimate frame rate
  float t0 = 0.0, t1 = 0.0;  // time variables for frame rate
  int idisp = 0, ndisp = 20; // control the text output refresh rate
  float ifrate;              // frame rate (average over nfr frames)
  struct timespec now;       // clock readout
  struct tm* ptm;
  float *timing = (float*) malloc(nfr * sizeof(float));

  for (ifr = 0; ifr < nfr; ifr++) timing[ifr] = 0.1; // init timing array
  ifr = 1;

  // ------- writes square rotating around center in mock image ----------
  float angle; 
  float r;
  long ii, jj;
  float x, y, x0, y0, xc, yc;

  long dtus = 1000; // update every 1ms
  float dangle = 0.02;
    
  angle = 0.0;
  r = 50.0;
  x0 = 0.5*imarray->md->size[0];
  y0 = 0.5*imarray->md->size[1];
  
  while (camconf->nleft > 0) {

	// ------------------------------------------------------------------------
	//                 estimating the current frame rate
	// ------------------------------------------------------------------------
	clock_gettime(CLOCK_REALTIME, &now); // what is the time ??
	ptm = gmtime(&(now.tv_sec));
	t1 = (float)(now.tv_nsec)*1e-9 + (float)(ptm->tm_sec);
	
	// estimate the frame rate
	timing[ifr] = t1-t0;
	t0 = t1;
	ifr++;
	if (ifr == nfr) ifr = 0;
    
	ifrate = 0.0;
	for (int i = 0; i < nfr; i++)
	  ifrate += timing[i];
    ifrate = (float)(nfr) / ifrate;
	camconf->frate = ifrate;
	
	// ------------------------------------------------------------------------
	//                   drawing inside the image
	// ------------------------------------------------------------------------
	xc = x0 + r*cos(angle);
	yc = y0 + r*sin(angle);
	
	imarray->md->write = 1; // set this flag to 1 when writing data

	for(ii=0; ii<imarray->md->size[0]; ii++)
	  for(jj=0; jj<imarray->md->size[1]; jj++) {
		x = 1.0*ii;
		y = 1.0*jj;
		float dx = x-xc;
		float dy = y-yc;
		imarray->array.F[ii*imarray->md->size[1]+jj] =
		  cos(0.03*dx)*cos(0.03*dy)*exp(-1.0e-4*(dx*dx+dy*dy));
	  }
	imarray->md->cnt1 = 0;
	imarray->md->cnt0++;
	
	ImageStreamIO_sempost(imarray, -1); 	// POST ALL SEMAPHORES
	imarray->md->write = 0;                  // Done writing data
				
	usleep(dtus);
	angle += dangle;
	if(angle > 2.0*3.141592)
	  angle -= 2.0*3.141592;

	// --------------------------- house-keeping -------------------------------
	idisp++;
	
	if (camconf->babort == 1) {
	  camconf->nleft = 0;
	  camconf->bstreamon = 0;
	  camconf->babort = 0;
	}
	if (camconf->bstreamon == 0)
	  camconf->nleft--; // decrement if not streaming

	if (idisp == ndisp) {
	  imarray->kw[1].value.numf = ifrate; // update keyword if it is time
	  idisp = 0;
	}
  }

  camconf->bacqon = 0;    // updating control flags before release
  camconf->bstreamon = 0; // updating control flags before release
  return NULL;
}

/* =========================================================================
 *                            Main program
 * ========================================================================= */
int main() {
  pthread_t tid_acqr;      // thread id for acquisition

  char cmdstring[LINESIZE];
  char serialcmd[LINESIZE];
  //char loginfo[LINESIZE];
  
  char str0[20];

  int ed = 0; // place holder for device?
  int cmdOK = 0;
  int wxsz, wysz; // window size
  initscr(); // start curses mode
  start_color();
  getmaxyx(stdscr, wysz, wxsz);
  init_pair(1, COLOR_RED, COLOR_BLACK);
  init_pair(2, COLOR_GREEN, COLOR_BLACK);
  init_pair(3, COLOR_YELLOW, COLOR_BLACK);
  
  // --------------------------------------------------------------------------
  //                        shared memory setup
  // --------------------------------------------------------------------------
  long naxis;       // number of axes
  uint8_t atype;    // data type
  uint32_t *imsize; // image size
  int shared;       // 1 if image in shared memory
  int NBkw;         // number of keywords
  
  // test image size will be 512 x 512
  naxis = 2;
  
  imarray = (IMAGE*) malloc(sizeof(IMAGE));
  imsize = (uint32_t *) malloc(sizeof(uint32_t)*naxis);
  cconf = (cam_config*) malloc(sizeof(cam_config));
  cam_config_init(cconf);
  
  imsize[0] = 128;
  imsize[1] = 128;
  atype = _DATATYPE_FLOAT;
  shared = 1;
  NBkw = 10;

  // create an image in shared memory
  ImageStreamIO_createIm_gpu(imarray, "imtest00", naxis, imsize, atype, -1,
							 shared, IMAGE_NB_SEMAPHORE, NBkw, MATH_DATA);
  free(imsize);

  strcpy(imarray->kw[0].name, "keyword_long");
  imarray->kw[0].type = 'L';
  imarray->kw[0].value.numl = 42;
  
  strcpy(imarray->kw[1].name, "Frame rate");
  imarray->kw[1].type = 'D';
  imarray->kw[1].value.numf = 0.0;
  
  strcpy(imarray->kw[2].name, "keyword_string");
  imarray->kw[2].type = 'S';
  strcpy(imarray->kw[2].value.valstr, "Hello!");


  // --------------------- set-up the prompt --------------------
  attron(COLOR_PAIR(2));
  printw("%s", dashline);
  printw("                   CAMERA CONTROL INTERACTIVE SHELL\n");
  printw("\nDid you launch this program from within a tmux as it is meant?\n");
  printw("\n");
  printw("%s", dashline);
  attroff(COLOR_PAIR(2));

  // -------------- open a handle to the device -----------------

  // ---------- open a fifo to send values to client ------------
  if (mkfifo(myfifout, 0777) != 0) printw("Could not create fifo!\n");
  
  // ---------------- command line interpreter ------------------
  
  for (;;) {
    cmdOK = 0;

	attron(COLOR_PAIR(3));
	move(wysz-3, 0);
	clrtoeol();
	printw("CAM > ");
	attroff(COLOR_PAIR(3));
	getstr(cmdstring);

	
    // ------------------------------------------------------------------------
	//                      command interpreter
    // ------------------------------------------------------------------------
	
    if (cmdOK == 0) // -------- STATUS ---------
      if (strncmp(cmdstring, "status", strlen("status")) == 0) {
		sprintf(serialcmd, "status raw");
		server_command(ed, serialcmd);
		//sscanf(outbuf, "%s", str0);
		sprintf(str0, "%s", "perfect!");
		attron(COLOR_PAIR(2));
		printw("status: %s\n", str0);
		attroff(COLOR_PAIR(2));
		send_to_fifo(myfifout, str0);
		cmdOK = 1;
      }
	
    if (cmdOK == 0)
      if (strncmp(cmdstring, "help", strlen("help")) == 0) {
		print_help();
		cmdOK = 1;
      }

	if (cmdOK == 0)
      if (strncmp(cmdstring, "stream", strlen("stream")) == 0) {
		if (cconf->bstreamon == 0) {
		  cconf->bacqon = 1;
		  cconf->bstreamon = 1;
		  cconf->nleft = 1;
		  attron(COLOR_PAIR(1));
		  printw("streaming\n");
		  attroff(COLOR_PAIR(1));
		  pthread_create(&tid_acqr, NULL, acquire, cconf);
		}
		cmdOK = 1;
      }

    if (cmdOK == 0)
      if (strncmp(cmdstring, "abort", strlen("abort")) == 0) {
		if (cconf->bacqon == 1) {
		  cconf->babort = 1;
		  attron(COLOR_PAIR(1));
		  printw("acquisition aborted\n");
		  attroff(COLOR_PAIR(1));
		}
		cmdOK = 1;
      }
	
    if (cmdOK == 0)
      if (strncmp(cmdstring, "get_frate", strlen("get_frate")) == 0) {
		attron(COLOR_PAIR(2));
		printw("Frame rate: %.2f Hz\n", cconf->frate);
		attroff(COLOR_PAIR(2));
		cmdOK = 1;
      }
	
    if (cmdOK == 0)
      if (strncmp(cmdstring, "quit", strlen("quit")) == 0) {

		attron(COLOR_PAIR(1) | A_BOLD);
		printw("Camera shell closed!\n");
		attroff(COLOR_PAIR(1) | A_BOLD);
		unlink(myfifout);

		free(imarray);
		free(cconf);
		cconf = NULL;
		getch();
		endwin();
		exit(0);
      }
    
    if (cmdOK == 0) {
	  attron(COLOR_PAIR(1));
      printw("Unkown command: %s\n", cmdstring);
	  attroff(COLOR_PAIR(1));
	  
      print_help();
    }
  }
  
  unlink(myfifout);
  exit(0);
}

