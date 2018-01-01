/*
 This file is part of program wsprd, a detector/demodulator/decoder
 for the Weak Signal Propagation Reporter (WSPR) mode.  Presently
 implemented for WSPR-2; needs some changes for WSPR-15.
 
 File name: wsprd.c

 Copyright 2001-2015, Joe Taylor, K1JT
 Copyright 2014-2015, Steven Franke, K9AN

 License: GNU GPL v3
 
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <fftw3.h>

#include "fano.h"
#include "wsprd_utils.h"

#define max(x,y) ((x) > (y) ? (x) : (y))
// Possible PATIENCE options: FFTW_ESTIMATE, FFTW_ESTIMATE_PATIENT,
// FFTW_MEASURE, FFTW_PATIENT, FFTW_EXHAUSTIVE
#define PATIENCE FFTW_ESTIMATE
fftw_plan PLAN1,PLAN2,PLAN3;

unsigned char pr3[162]=
{1,1,0,0,0,0,0,0,1,0,0,0,1,1,1,0,0,0,1,0,
 0,1,0,1,1,1,1,0,0,0,0,0,0,0,1,0,0,1,0,1,
 0,0,0,0,0,0,1,0,1,1,0,0,1,1,0,1,0,0,0,1,
 1,0,1,0,0,0,0,1,1,0,1,0,1,0,1,0,1,0,0,1,
 0,0,1,0,1,1,0,0,0,1,1,0,1,0,1,0,0,0,1,0,
 0,0,0,0,1,0,0,1,0,0,1,1,1,0,1,1,0,0,1,1,
 0,1,0,0,0,1,1,1,0,0,0,0,0,1,0,1,0,0,1,1,
 0,0,0,0,0,0,0,1,1,0,1,0,1,1,0,0,0,1,1,0,
 0,0};

unsigned long nr;

//***************************************************************************
unsigned long readc2file(char *ptr_to_infile, double *idat, double *qdat, 
			 double *freq)
{
  float buffer[2*65536];
  double dfreq;
  int i,ntrmin;
  char *c2file[15];
  FILE* fp;

  fp=fopen(ptr_to_infile,"r");
  fp = fopen(ptr_to_infile,"rb");
  if (fp == NULL) {
    fprintf(stderr, "Cannot open data file '%s'\n", ptr_to_infile);
    return 1;
  }
  unsigned long nread=fread(c2file,sizeof(char),14,fp);
  nread=fread(&ntrmin,sizeof(int),1,fp);
  nread=fread(&dfreq,sizeof(double),1,fp);
  *freq=dfreq;
  nread=fread(buffer,sizeof(float),2*45000,fp);

  for(i=0; i<45000; i++) {
    idat[i]=buffer[2*i];
    qdat[i]=-buffer[2*i+1];
  }
    
  if( nread == 2*45000 ) {
    return nread/2;
  } else {
    return 1;
  }
}

//***************************************************************************
unsigned long readwavfile(char *ptr_to_infile, double *idat, double *qdat )
{
  unsigned long i, j;
  int nfft1=1474560;;
  int nfft2=nfft1/32;                  //nfft2=46080
  int nh2=nfft2/2;
  double df=12000.0/nfft1;
  int i0=1500.0/df+0.5;
  double *realin;
  fftw_complex *fftin, *fftout;

  FILE *fp;
  unsigned long npoints=114*12000;
  short int *buf2;
  buf2 = malloc(npoints*sizeof(short int));

  fp = fopen(ptr_to_infile,"rb");
  if (fp == NULL) {
    fprintf(stderr, "Cannot open data file '%s'\n", ptr_to_infile);
    return 1;
  }

  nr=fread(buf2,2,22,fp);            //Read and ignore header
  nr=fread(buf2,2,npoints,fp);       //Read raw data
  fclose(fp);

  realin=(double*) fftw_malloc(sizeof(double)*nfft1);
  fftout=(fftw_complex*) fftw_malloc(sizeof(fftw_complex)*nfft1);
  PLAN1 = fftw_plan_dft_r2c_1d(nfft1, realin, fftout, PATIENCE);
  
  for (i=0; i<npoints; i++) {
    realin[i]=buf2[i]/32768.0;
  }

  for (i=npoints; i<nfft1; i++) {
    realin[i]=0.0;
  }

  free(buf2);
  fftw_execute(PLAN1);  
  fftw_free(realin);
 
  fftin=(fftw_complex*) fftw_malloc(sizeof(fftw_complex)*nfft2);

  for (i=0; i<nfft2; i++) { 
   j=i0+i;
    if( i>nh2 ) j=j-nfft2;
    fftin[i][0]=fftout[j][0];
    fftin[i][1]=fftout[j][1];
  }

  fftw_free(fftout);
  fftout=(fftw_complex*) fftw_malloc(sizeof(fftw_complex)*nfft2);
  PLAN2 = fftw_plan_dft_1d(nfft2, fftin, fftout, FFTW_BACKWARD, PATIENCE);
  fftw_execute(PLAN2);
    
  for (i=0; i<nfft2; i++) {
    idat[i]=fftout[i][0]/1000.0;
    qdat[i]=fftout[i][1]/1000.0;
  }

  fftw_free(fftin);
  fftw_free(fftout);
  return nfft2;
}

//***************************************************************************
void sync_and_demodulate(double *id, double *qd, long np, 
			 unsigned char *symbols, float *f1, float fstep,
			 int *shift1, int lagmin, int lagmax, int lagstep,
			 float *drift1, int symfac, float *sync, int mode)
{
/***********************************************************************
* mode = 0: no frequency or drift search. find best time lag.          *
*        1: no time lag or drift search. find best frequency.          *
*        2: no frequency or time lag search. calculate soft-decision   *
*           symbols using passed frequency and shift.                  *
************************************************************************/

  float dt=1.0/375.0, df=375.0/256.0,fbest=0.0;
  int i, j, k;
  double pi=4.*atan(1.0);
  float f0=0.0,fp,ss;
  int lag;
    
  double i0[162],q0[162],i1[162],q1[162],i2[162],q2[162],i3[162],q3[162];
  double p0,p1,p2,p3,cmet,totp,syncmax,fac;
  double c0[256],s0[256],c1[256],s1[256],c2[256],s2[256],c3[256],s3[256];
  double dphi0, cdphi0, sdphi0, dphi1, cdphi1, sdphi1, dphi2, cdphi2, sdphi2,
    dphi3, cdphi3, sdphi3;
  float fsum=0.0, f2sum=0.0, fsymb[162];
  int best_shift = 0, ifreq;
  int ifmin=0, ifmax=0;
  
  syncmax=-1e30;
  if( mode == 0 ) {ifmin=0; ifmax=0; fstep=0.0; f0=*f1;}
  if( mode == 1 ) {lagmin=*shift1;lagmax=*shift1;ifmin=-5;ifmax=5;f0=*f1;}
  if( mode == 2 ) {lagmin=*shift1;lagmax=*shift1;ifmin=0;ifmax=0;f0=*f1;}

  for(ifreq=ifmin; ifreq<=ifmax; ifreq++) {
    f0=*f1+ifreq*fstep;
    for(lag=lagmin; lag<=lagmax; lag=lag+lagstep) {
      ss=0.0;
      totp=0.0;
      for (i=0; i<162; i++) {
	fp = f0 + ((float)*drift1/2.0)*((float)i-81.0)/81.0;
	dphi0=2*pi*(fp-1.5*df)*dt;
	cdphi0=cos(dphi0);
	sdphi0=sin(dphi0);

	dphi1=2*pi*(fp-0.5*df)*dt;
	cdphi1=cos(dphi1);
	sdphi1=sin(dphi1);

	dphi2=2*pi*(fp+0.5*df)*dt;
	cdphi2=cos(dphi2);
	sdphi2=sin(dphi2);

	dphi3=2*pi*(fp+1.5*df)*dt;
	cdphi3=cos(dphi3);
	sdphi3=sin(dphi3);
                
	c0[0]=1; s0[0]=0; 
	c1[0]=1; s1[0]=0;
	c2[0]=1; s2[0]=0; 
	c3[0]=1; s3[0]=0;
                
	for (j=1; j<256; j++) {
	  c0[j]=c0[j-1]*cdphi0 - s0[j-1]*sdphi0;
	  s0[j]=c0[j-1]*sdphi0 + s0[j-1]*cdphi0;
	  c1[j]=c1[j-1]*cdphi1 - s1[j-1]*sdphi1;
	  s1[j]=c1[j-1]*sdphi1 + s1[j-1]*cdphi1;
	  c2[j]=c2[j-1]*cdphi2 - s2[j-1]*sdphi2;
	  s2[j]=c2[j-1]*sdphi2 + s2[j-1]*cdphi2;
	  c3[j]=c3[j-1]*cdphi3 - s3[j-1]*sdphi3;
	  s3[j]=c3[j-1]*sdphi3 + s3[j-1]*cdphi3;
	}
                
	i0[i]=0.0; q0[i]=0.0;
	i1[i]=0.0; q1[i]=0.0;
	i2[i]=0.0; q2[i]=0.0;
	i3[i]=0.0; q3[i]=0.0;
 
	for (j=0; j<256; j++) {
	  k=lag+i*256+j;
	  if( (k>0) & (k<np) ) {
	    i0[i]=i0[i] + id[k]*c0[j] + qd[k]*s0[j];
	    q0[i]=q0[i] - id[k]*s0[j] + qd[k]*c0[j];
	    i1[i]=i1[i] + id[k]*c1[j] + qd[k]*s1[j];
	    q1[i]=q1[i] - id[k]*s1[j] + qd[k]*c1[j];
	    i2[i]=i2[i] + id[k]*c2[j] + qd[k]*s2[j];
	    q2[i]=q2[i] - id[k]*s2[j] + qd[k]*c2[j];
	    i3[i]=i3[i] + id[k]*c3[j] + qd[k]*s3[j];
	    q3[i]=q3[i] - id[k]*s3[j] + qd[k]*c3[j];
	  }
	}
	p0=i0[i]*i0[i] + q0[i]*q0[i];
	p1=i1[i]*i1[i] + q1[i]*q1[i];
	p2=i2[i]*i2[i] + q2[i]*q2[i];
	p3=i3[i]*i3[i] + q3[i]*q3[i];
	
	p0=sqrt(p0);
	p1=sqrt(p1);
	p2=sqrt(p2);
	p3=sqrt(p3);

	totp=totp+p0+p1+p2+p3;
	cmet=(p1+p3)-(p0+p2);
	ss=ss+cmet*(2*pr3[i]-1);                
	if( mode == 2) {                 //Compute soft symbols
	  if(pr3[i]) {
	    fsymb[i]=p3-p1;
	  } else {
	    fsymb[i]=p2-p0;
	  }
	}	
      }
            
      if( ss/totp > syncmax ) {          //Save best parameters
	syncmax=ss/totp;
	best_shift=lag;
	fbest=f0;
      }
    } // lag loop
  } //freq loop

  if( mode <=1 ) {                       //Send best params back to caller
    *sync=syncmax;
    *shift1=best_shift;
    *f1=fbest;
    return;
  }
    
  if( mode == 2 ) {
    *sync=syncmax;
    for (i=0; i<162; i++) {              //Normalize the soft symbols
      fsum=fsum+fsymb[i]/162.0;
      f2sum=f2sum+fsymb[i]*fsymb[i]/162.0;
    }
    fac=sqrt(f2sum-fsum*fsum);
    for (i=0; i<162; i++) {
      fsymb[i]=symfac*fsymb[i]/fac;
      if( fsymb[i] > 127) fsymb[i]=127.0;
      if( fsymb[i] < -128 ) fsymb[i]=-128.0;
      symbols[i]=fsymb[i] + 128;
    }
    return;
  }
  return;
}

//***************************************************************************
void usage(void)
{
  printf("Usage: wsprd [options...] infile\n");
  printf("       infile must have suffix .wav or .c2\n");
  printf("\n");
  printf("Options:\n");
  printf("       -e x (x is transceiver dial frequency error in Hz)\n");
  printf("       -f x (x is transceiver dial frequency in MHz)\n");
// blanking is not yet implemented. The options are accepted for compatibility
// with development version of wsprd.
//    printf("       -t n (n is blanking duration in milliseconds)\n");
//    printf("       -b n (n is pct of time that is blanked)\n");
  printf("       -H do not use (or update) the hash table\n");
  printf("       -n write noise estimates to file noise.dat\n");
  printf("       -q quick mode - doesn't dig deep for weak signals\n");
  printf("       -v verbose mode\n");
  printf("       -w wideband mode - decode signals within +/- 150 Hz of center\n");
}

//***************************************************************************
int main(int argc, char *argv[])
{
  extern char *optarg;
  extern int optind;
  int i,j,k;
  unsigned char *symbols, *decdata;
  signed char message[]={-9,13,-35,123,57,-39,64,0,0,0,0};
  char *callsign,*grid,*grid6, *call_loc_pow, *cdbm;
  char *ptr_to_infile,*ptr_to_infile_suffix;
  char uttime[5],date[7];
  int c,delta,nfft2=65536,verbose=0,quickmode=0,writenoise=0,usehashtable=1;
  int shift1, lagmin, lagmax, lagstep, worth_a_try, not_decoded, nadd, ndbm;
  int32_t n1, n2, n3;
  unsigned int nbits;
  unsigned int npoints, metric, maxcycles, cycles, maxnp;
  float df=375.0/256.0/2;
  float freq0[200],snr0[200],drift0[200],sync0[200];
  int shift0[200];
  float dt=1.0/375.0;
  double dialfreq_cmdline=0.0, dialfreq;
  float dialfreq_error=0.0;
  float fmin=-110, fmax=110;
  float f1, fstep, sync1, drift1, tblank=0, fblank=0;
  double *idat, *qdat;
  clock_t t0,t00;
  double tfano=0.0,treadwav=0.0,tcandidates=0.0,tsync0=0.0;
  double tsync1=0.0,tsync2=0.0,ttotal=0.0;

// Parameters used for performance-tuning:
  maxcycles=10000;                         //Fano timeout limit
  double minsync1=0.10;                    //First sync limit
  double minsync2=0.12;                    //Second sync limit
  int iifac=3;                             //Step size in final DT peakup
  int symfac=45;                           //Soft-symbol normalizing factor
  int maxdrift=4;                          //Maximum (+/-) drift
  double minrms=52.0 * (symfac/64.0);      //Final test for palusible decoding
  delta=60;                                //Fano threshold step

  t00=clock();
  fftw_complex *fftin, *fftout;
#include "./mettab.c"

// Check for an optional FFTW wisdom file
  FILE *fp_fftw_wisdom_file;
  if ((fp_fftw_wisdom_file = fopen("fftw_wisdom_wsprd", "r"))) {
    fftw_import_wisdom_from_file(fp_fftw_wisdom_file);
    fclose(fp_fftw_wisdom_file);
  }

  idat=malloc(sizeof(double)*nfft2);
  qdat=malloc(sizeof(double)*nfft2);

  while ( (c = getopt(argc, argv, "b:e:f:Hnqt:wv")) !=-1 ) {
    switch (c) {
    case 'b':
      fblank = strtof(optarg,NULL);
      break;
    case 'e':
      dialfreq_error = strtof(optarg,NULL);   // units of Hz
      // dialfreq_error = dial reading - actual, correct frequency
      break;
    case 'f':
      dialfreq_cmdline = strtod(optarg,NULL); // units of MHz
      break;
    case 'H':
      usehashtable = 0;
      break;
    case 'n':
      writenoise = 1;
      break;
    case 'q':
      quickmode = 1;
      break;
    case 't':
      tblank = strtof(optarg,NULL);
      break;
    case 'v':
      verbose = 1;
      break;
    case 'w':
      fmin=-150.0;
      fmax=150.0;
      break;
    case '?':
      usage();
      return 1;
    }
  }

  if( optind+1 > argc) {
    usage();
    return 1;
  } else {
    ptr_to_infile=argv[optind];
  }

  FILE *fall_wspr, *fwsprd, *fhash, *ftimer;
  FILE *fdiag;
  fall_wspr=fopen("/dev/null", "w");
  fwsprd=fopen("/dev/null","w");
  fdiag=fopen("/dev/null","a");

  if((ftimer=fopen("wsprd_timer","r"))) {
    //Accumulate timing data
    nr=fscanf(ftimer,"%lf %lf %lf %lf %lf %lf %lf",
	   &treadwav,&tcandidates,&tsync0,&tsync1,&tsync2,&tfano,&ttotal);
    fclose(ftimer);
  }
  ftimer=fopen("/dev/null","w");

  if( strstr(ptr_to_infile,".wav") || strcmp(ptr_to_infile, "/dev/stdin") == 0) {
    ptr_to_infile_suffix=strstr("test.wav",".wav");

    t0 = clock();
    npoints=readwavfile(ptr_to_infile, idat, qdat);
    treadwav += (double)(clock()-t0)/CLOCKS_PER_SEC;

    if( npoints == 1 ) {
      return 1;
    }
    dialfreq=dialfreq_cmdline - (dialfreq_error*1.0e-06);
  } else if ( strstr(ptr_to_infile,".c2") !=0 )  {
    ptr_to_infile_suffix=strstr(ptr_to_infile,".c2");
    npoints=readc2file(ptr_to_infile, idat, qdat, &dialfreq);
    if( npoints == 1 ) {
      return 1;
    }
    dialfreq -= (dialfreq_error*1.0e-06);
  } else {
    printf("Error: Failed to open %s\n",ptr_to_infile);
    printf("WSPR file must have suffix .wav or .c2\n");
    return 1;
  }

// Parse date and time from given filename
  strncpy(date,ptr_to_infile_suffix-11,6);
  strncpy(uttime,ptr_to_infile_suffix-4,4);
  date[6]='\0';
  uttime[4]='\0';

// Do windowed ffts over 2 symbols, stepped by half symbols
  int nffts=4*floor(npoints/512)-1;
  fftin=(fftw_complex*) fftw_malloc(sizeof(fftw_complex)*512);
  fftout=(fftw_complex*) fftw_malloc(sizeof(fftw_complex)*512);
  PLAN3 = fftw_plan_dft_1d(512, fftin, fftout, FFTW_FORWARD, PATIENCE);
    
  float ps[512][nffts];
  float w[512];
  for(i=0; i<512; i++) {
    w[i]=sin(0.006135923*i);
  }

  memset(ps,0.0, sizeof(float)*512*nffts);
  for (i=0; i<nffts; i++) {
    for(j=0; j<512; j++ ) {
      k=i*128+j;
      fftin[j][0]=idat[k] * w[j];
      fftin[j][1]=qdat[k] * w[j];
    }
    fftw_execute(PLAN3);
    for (j=0; j<512; j++ ) {
      k=j+256;
      if( k>511 )
	k=k-512;
      ps[j][i]=fftout[k][0]*fftout[k][0]+fftout[k][1]*fftout[k][1];
    }
  }

  fftw_free(fftin);
  fftw_free(fftout);

// Compute average spectrum
  float psavg[512];
  memset(psavg,0.0, sizeof(float)*512);
  for (i=0; i<nffts; i++) {
    for (j=0; j<512; j++) {
      psavg[j]=psavg[j]+ps[j][i];
    }
  }

// Smooth with 7-point window and limit spectrum to +/-150 Hz
  int window[7]={1,1,1,1,1,1,1};
  float smspec[411];
  for (i=0; i<411; i++) {
    smspec[i]=0.0;
    for(j=-3; j<=3; j++) {
      k=256-205+i+j;
      smspec[i]=smspec[i]+window[j+3]*psavg[k];
    }
  }

// Sort spectrum values, then pick off noise level as a percentile
  float tmpsort[411];
  for (j=0; j<411; j++) {
    tmpsort[j]=smspec[j];
  }
  qsort(tmpsort, 411, sizeof(float), floatcomp);

// Noise level of spectrum is estimated as 123/411= 30'th percentile
  float noise_level = tmpsort[122];

// Renormalize spectrum so that (large) peaks represent an estimate of snr
  float min_snr_neg33db = pow(10.0,(-33+26.5)/10.0);
  for (j=0; j<411; j++) {
    smspec[j]=smspec[j]/noise_level - 1.0;
    if( smspec[j] < min_snr_neg33db) smspec[j]=0.1;
    continue;
  }

// Find all local maxima in smoothed spectrum.
  for (i=0; i<200; i++) {
    freq0[i]=0.0;
    snr0[i]=0.0;
    drift0[i]=0.0;
    shift0[i]=0;
    sync0[i]=0.0;
  }

  int npk=0;
  for(j=1; j<410; j++) {
    if((smspec[j]>smspec[j-1]) && (smspec[j]>smspec[j+1]) && (npk<200)) {
      freq0[npk]=(j-205)*df;
      snr0[npk]=10*log10(smspec[j])-26.5;
      npk++;
    }
  }

// Compute corrected fmin, fmax, accounting for dial frequency error
  fmin += dialfreq_error;    // dialfreq_error is in units of Hz
  fmax += dialfreq_error;

// Don't waste time on signals outside of the range [fmin,fmax].
  i=0;
  for( j=0; j<npk; j++) {
    if( freq0[j] >= fmin && freq0[j] <= fmax ) {
      freq0[i]=freq0[j];
      snr0[i]=snr0[j];
      i++;
    }
  }
  npk=i;

  t0=clock();
/* Make coarse estimates of shift (DT), freq, and drift

  * Look for time offsets up to +/- 8 symbols (about +/- 5.4 s) relative 
    to nominal start time, which is 2 seconds into the file

  * Calculates shift relative to the beginning of the file

  * Negative shifts mean that signal started before start of file

  * The program prints DT = shift-2 s

  * Shifts that cause sync vector to fall off of either end of the data 
    vector are accommodated by "partial decoding", such that missing 
    symbols produce a soft-decision symbol value of 128 

  * The frequency drift model is linear, deviation of +/- drift/2 over the
    span of 162 symbols, with deviation equal to 0 at the center of the 
    signal vector. 
*/

  int idrift,ifr,if0,ifd,k0;
  int kindex;
  float smax,ss,pow,p0,p1,p2,p3;
  for(j=0; j<npk; j++) {                              //For each candidate...
    smax=-1e30;
    if0=freq0[j]/df+256;
    for (ifr=if0-1; ifr<=if0+1; ifr++) {                      //Freq search
      for( k0=-10; k0<22; k0++) {                             //Time search
	for (idrift=-maxdrift; idrift<=maxdrift; idrift++) {  //Drift search
	  ss=0.0;
	  pow=0.0;
	  for (k=0; k<162; k++) {                             //Sum over symbols
	    ifd=ifr+((float)k-81.0)/81.0*( (float)idrift )/(2.0*df);
	    kindex=k0+2*k;
	    if( kindex < nffts ) {
	      p0=ps[ifd-3][kindex];
	      p1=ps[ifd-1][kindex];
	      p2=ps[ifd+1][kindex];
	      p3=ps[ifd+3][kindex];

	      p0=sqrt(p0);
	      p1=sqrt(p1);
	      p2=sqrt(p2);
	      p3=sqrt(p3);

	      ss=ss+(2*pr3[k]-1)*((p1+p3)-(p0+p2));
	      pow=pow+p0+p1+p2+p3;
	      sync1=ss/pow;
	    }
	  }
	  if( sync1 > smax ) {                  //Save coarse parameters
	    smax=sync1;
	    shift0[j]=128*(k0+1);
	    drift0[j]=idrift;
	    freq0[j]=(ifr-256)*df;
	    sync0[j]=sync1;
	  }
	}
      }
    }
  }
  tcandidates += (double)(clock()-t0)/CLOCKS_PER_SEC;

  nbits=81;
  symbols=malloc(sizeof(char)*nbits*2);
  memset(symbols,0,sizeof(char)*nbits*2);
  decdata=malloc((nbits+7)/8);
  grid=malloc(sizeof(char)*5);
  grid6=malloc(sizeof(char)*7);
  callsign=malloc(sizeof(char)*13);
  call_loc_pow=malloc(sizeof(char)*23);
  cdbm=malloc(sizeof(char)*3);
  float allfreqs[npk];
  memset(allfreqs,0,sizeof(float)*npk);
  char allcalls[npk][13];
  memset(allcalls,0,sizeof(char)*npk*13);
  memset(grid,0,sizeof(char)*5);
  memset(grid6,0,sizeof(char)*7);
  memset(callsign,0,sizeof(char)*13);
  memset(call_loc_pow,0,sizeof(char)*23);
  memset(cdbm,0,sizeof(char)*3);
  char hashtab[32768][13];
  memset(hashtab,0,sizeof(char)*32768*13);
  uint32_t nhash( const void *, size_t, uint32_t);
  int nh;
    
  if( usehashtable ) {
    char line[80], hcall[12];
    if( (fhash=fopen("hashtable.txt","r+")) ) {
      while (fgets(line, sizeof(line), fhash) != NULL) {
	sscanf(line,"%d %s",&nh,hcall);
	strcpy(*hashtab+nh*13,hcall);
      }
    } else {
      fhash=fopen("/dev/null","w+");
    }
    fclose(fhash);
  }
    
  int uniques=0, noprint=0;
/*    
 Refine the estimates of freq, shift using sync as a metric.
 Sync is calculated such that it is a float taking values in the range
 [0.0,1.0].
        
 Function sync_and_demodulate has three modes of operation
 mode is the last argument:

      0 = no frequency or drift search. find best time lag.
      1 = no time lag or drift search. find best frequency.
      2 = no frequency or time lag search. Calculate soft-decision 
          symbols using passed frequency and shift.

NB: best possibility for OpenMP may be here: several worker threads
could each work on one candidate at a time.
*/

  for (j=0; j<npk; j++) {
    f1=freq0[j];
    drift1=drift0[j];
    shift1=shift0[j];
    sync1=sync0[j];

// Fine search for best sync lag (mode 0)
    fstep=0.0;
    lagmin=shift1-144;
    lagmax=shift1+144;
    lagstep=8;
    if(quickmode) lagstep=16;
    t0 = clock();
    sync_and_demodulate(idat, qdat, npoints, symbols, &f1, fstep, &shift1, 
		    lagmin, lagmax, lagstep, &drift1, symfac, &sync1, 0);
    tsync0 += (double)(clock()-t0)/CLOCKS_PER_SEC;

// Fine search for frequency peak (mode 1)
    fstep=0.1;
    t0 = clock();
    sync_and_demodulate(idat, qdat, npoints, symbols, &f1, fstep, &shift1, 
		     lagmin, lagmax, lagstep, &drift1, symfac, &sync1, 1);
    tsync1 += (double)(clock()-t0)/CLOCKS_PER_SEC;

    if( sync1 > minsync1 ) {
      worth_a_try = 1;
    } else {
      worth_a_try = 0;
    }

    int idt=0, ii=0, jiggered_shift;
    uint32_t ihash;
    double y,sq,rms;
    not_decoded=1;

    while ( worth_a_try && not_decoded && idt<=(128/iifac)) {
      ii=(idt+1)/2;
      if( idt%2 == 1 ) ii=-ii;
      ii=iifac*ii;
      jiggered_shift=shift1+ii;

// Use mode 2 to get soft-decision symbols
      t0 = clock();
      sync_and_demodulate(idat, qdat, npoints, symbols, &f1, fstep, 
	 &jiggered_shift, lagmin, lagmax, lagstep, &drift1, symfac, 
			  &sync1, 2);
      tsync2 += (double)(clock()-t0)/CLOCKS_PER_SEC;

      sq=0.0;
      for(i=0; i<162; i++) {
	y=(double)symbols[i] - 128.0;
	sq += y*y;
      }
      rms=sqrt(sq/162.0);

      if((sync1 > minsync2) && (rms > minrms)) {
	deinterleave(symbols);
	t0 = clock();
	  not_decoded = fano(&metric,&cycles,&maxnp,decdata,symbols,nbits,
			     mettab,delta,maxcycles);
	tfano += (double)(clock()-t0)/CLOCKS_PER_SEC;

	/* ### Used for timing tests:
	if(not_decoded) fprintf(fdiag,
	    "%6s %4s %4.1f %3.0f %4.1f %10.7f  %-18s %2d %5u %4d %6.1f %2d\n",
	    date,uttime,sync1*10,snr0[j], shift1*dt-2.0, dialfreq+(1500+f1)/1e6,
	    "@                 ", (int)drift1, cycles/81, ii, rms, maxnp);
	*/
      }
      idt++;
      if( quickmode ) break;  
    }

    if( worth_a_try && !not_decoded ) {
      for(i=0; i<11; i++) {
	if( decdata[i]>127 ) {
	  message[i]=decdata[i]-256;
	} else {
	  message[i]=decdata[i];
	}
      }

      unpack50(message,&n1,&n2);
      unpackcall(n1,callsign);
      unpackgrid(n2, grid);
      int ntype = (n2&127) - 64;

/*
 Based on the value of ntype, decide whether this is a Type 1, 2, or 
 3 message.

 * Type 1: 6 digit call, grid, power - ntype is positive and is a member 
         of the set {0,3,7,10,13,17,20...60}

 * Type 2: extended callsign, power - ntype is positive but not
         a member of the set of allowed powers

 * Type 3: hash, 6 digit grid, power - ntype is negative.
*/

      if( (ntype >= 0) && (ntype <= 62) ) {
	int nu=ntype%10;
	if( nu == 0 || nu == 3 || nu == 7 ) {
	  ndbm=ntype;
	  memset(call_loc_pow,0,sizeof(char)*23);
	  sprintf(cdbm,"%2d",ndbm);
	  strncat(call_loc_pow,callsign,strlen(callsign));
	  strncat(call_loc_pow," ",1);
	  strncat(call_loc_pow,grid,4);
	  strncat(call_loc_pow," ",1);
	  strncat(call_loc_pow,cdbm,2);
	  strncat(call_loc_pow,"\0",1);
                    
	  ihash=nhash(callsign,strlen(callsign),(uint32_t)146);
	  strcpy(*hashtab+ihash*13,callsign);

	  noprint=0;
	} else {
	  nadd=nu;
	  if( nu > 3 ) nadd=nu-3;
	  if( nu > 7 ) nadd=nu-7;
	  n3=n2/128+32768*(nadd-1);
	  unpackpfx(n3,callsign);
	  ndbm=ntype-nadd;

	  memset(call_loc_pow,0,sizeof(char)*23);
	  sprintf(cdbm,"%2d",ndbm);
	  strncat(call_loc_pow,callsign,strlen(callsign));
	  strncat(call_loc_pow," ",1);
	  strncat(call_loc_pow,cdbm,2);
	  strncat(call_loc_pow,"\0",1);
                    
	  ihash=nhash(callsign,strlen(callsign),(uint32_t)146);
	  strcpy(*hashtab+ihash*13,callsign);

	  noprint=0;
	}
      } else if ( ntype < 0 ) {
	ndbm=-(ntype+1);
	memset(grid6,0,sizeof(char)*7);
	strncat(grid6,callsign+5,1);
	strncat(grid6,callsign,5);
	ihash=(n2-ntype-64)/128;
	if( strncmp(hashtab[ihash],"\0",1) != 0 ) {
	  sprintf(callsign,"<%s>",hashtab[ihash]);
	} else {
	  sprintf(callsign,"%5s","<...>");
	}

	memset(call_loc_pow,0,sizeof(char)*23);
	sprintf(cdbm,"%2d",ndbm);
	strncat(call_loc_pow,callsign,strlen(callsign));
	strncat(call_loc_pow," ",1);
	strncat(call_loc_pow,grid6,strlen(grid6));
	strncat(call_loc_pow," ",1);
	strncat(call_loc_pow,cdbm,2);
	strncat(call_loc_pow,"\0",1);
                
	noprint=0;
                
// I don't know what to do with these... They show up as "A000AA" grids.
	if( ntype == -64 ) noprint=1;
	
      }
            
// Remove dupes (same callsign and freq within 1 Hz)
      int dupe=0;
      for (i=0; i<npk; i++) {
	if(!strcmp(callsign,allcalls[i]) && 
	   (fabs(f1-allfreqs[i]) <1.0)) dupe=1;
      }
      if( (verbose || !dupe) && !noprint) {
	uniques++;
	strcpy(allcalls[uniques],callsign);
	allfreqs[uniques]=f1;
// Add an extra space at the end of each line so that wspr-x doesn't 
// truncate the power (TNX to DL8FCL!)

	/*
	printf("%4s %3.0f %4.1f %10.6f %2d  %-s \n",
	       uttime, snr0[j],(shift1*dt-2.0), dialfreq+(1500+f1)/1e6,
	       (int)drift1, call_loc_pow);
	*/
	
	printf("%6s %4s %3.0f %3.0f %4.1f %10.7f  %-22s %2d %5u %4d\n",
		date,uttime,sync1*10,snr0[j],
		shift1*dt-2.0, dialfreq+(1500+f1)/1e6,
		call_loc_pow, (int)drift1, cycles/81, ii);

	/*
	fprintf(fwsprd,"%6s %4s %3d %3.0f %4.1f %10.6f  %-22s %2d %5u %4d\n",
		date,uttime,(int)(sync1*10),snr0[j],
		shift1*dt-2.0, dialfreq+(1500+f1)/1e6,
		call_loc_pow, (int)drift1, cycles/81, ii);
	*/

/* For timing tests

	fprintf(fdiag,
	  "%6s %4s %4.1f %3.0f %4.1f %10.7f  %-18s %2d %5u %4d %6.1f\n",
	  date,uttime,sync1*10,snr0[j],
	  shift1*dt-2.0, dialfreq+(1500+f1)/1e6,
	  call_loc_pow, (int)drift1, cycles/81, ii, rms);
*/
      }
    }
  }

  if ((fp_fftw_wisdom_file = fopen("/dev/null", "w"))) {
    fftw_export_wisdom_to_file(fp_fftw_wisdom_file);
    fclose(fp_fftw_wisdom_file);
  }

  ttotal += (double)(clock()-t00)/CLOCKS_PER_SEC;

  fprintf(ftimer,"%7.2f %7.2f %7.2f %7.2f %7.2f %7.2f %7.2f\n\n",
	  treadwav,tcandidates,tsync0,tsync1,tsync2,tfano,ttotal);

  fprintf(ftimer,"Code segment        Seconds   Frac\n");
  fprintf(ftimer,"-----------------------------------\n");
  fprintf(ftimer,"readwavfile        %7.2f %7.2f\n",treadwav,treadwav/ttotal);
  fprintf(ftimer,"Coarse DT f0 f1    %7.2f %7.2f\n",tcandidates,
	                                            tcandidates/ttotal);
  fprintf(ftimer,"sync_and_demod(0)  %7.2f %7.2f\n",tsync0,tsync0/ttotal);
  fprintf(ftimer,"sync_and_demod(1)  %7.2f %7.2f\n",tsync1,tsync1/ttotal);
  fprintf(ftimer,"sync_and_demod(2)  %7.2f %7.2f\n",tsync2,tsync2/ttotal);
  fprintf(ftimer,"Fano decoder       %7.2f %7.2f\n",tfano,tfano/ttotal);
  fprintf(ftimer,"-----------------------------------\n");
  fprintf(ftimer,"Total              %7.2f %7.2f\n",ttotal,1.0);

  fclose(fall_wspr);
  fclose(fwsprd);
  fclose(fdiag);
  fclose(ftimer);
  fftw_destroy_plan(PLAN1);
  fftw_destroy_plan(PLAN2);
  fftw_destroy_plan(PLAN3);

  if( usehashtable ) {
    fhash=fopen("/dev/null","w");
    for (i=0; i<32768; i++) {
      if( strncmp(hashtab[i],"\0",1) != 0 ) {
	fprintf(fhash,"%5d %s\n",i,*hashtab+i*13);
      }
    }
    fclose(fhash);
  }
  if(fblank+tblank+writenoise == 999) return -1;  //Silence compiler warning
  return 0;
}
