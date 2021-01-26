/*
 *  Copyright (C) 2019 Tyson B. Littenberg (MSFC-ST12), Neil J. Cornish
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with with program; see the file COPYING. If not, write to the
 *  Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 *  MA  02111-1307  USA
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include <gsl/gsl_rng.h>
#include <gsl/gsl_randist.h>
#include <gsl/gsl_fft_real.h>

#include <LISA.h>

#include "GalacticBinary.h"
#include "GalacticBinaryIO.h"
#include "GalacticBinaryData.h"
#include "GalacticBinaryMath.h"
#include "GalacticBinaryModel.h"
#include "GalacticBinaryWaveform.h"

#define TUKEY_FILTER_LENGTH 1e5 //seconds
#define N_TDI_CHANNELS 3

static void tukey(double *data, double alpha, int N)
{
    int i, imin, imax;
    double filter;
    
    imin = (int)(alpha*(double)(N-1)/2.0);
    imax = (int)((double)(N-1)*(1.0-alpha/2.0));
    
    for(i=0; i< N; i++)
    {
        filter = 1.0;
        if(i < imin) filter = 0.5*(1.0+cos(M_PI*( (double)(i)/(double)(imin)-1.0 )));
        if(i>imax) filter = 0.5*(1.0+cos(M_PI*( (double)(i)/(double)(imin)-2.0/alpha+1.0 )));
        data[i] *= filter;
    }
    
}

void GalacticBinaryReadHDF5(struct Data *data, struct TDI *tdi)
{
    /* LDASOFT-formatted structure for TDI data */
    struct TDI *tdi_td = malloc(sizeof(struct TDI));
    
    LISA_Read_HDF5_LDC_TDI(tdi_td, data->fileName);
    
    /* Select time segment of full data set */
    double start_time = data->t0[0];
    double stop_time = start_time + data->T;
    double dt = tdi_td->delta;
    double Tobs = stop_time - start_time;
    int N = (int)floor(Tobs/dt);
    int NFFT = pow(2, floor( log2(N) ));
    int n_start = (int)floor(start_time/dt); /* first sample of time segment */
    
    /* Truncate time series to be NFFT samples */
    double *X = malloc(NFFT*sizeof(double));
    double *Y = malloc(NFFT*sizeof(double));
    double *Z = malloc(NFFT*sizeof(double));
    double *A = malloc(NFFT*sizeof(double));
    double *E = malloc(NFFT*sizeof(double));
    double *T = malloc(NFFT*sizeof(double));
    for(int n=0; n<NFFT; n++)
    {
        int m = n_start+n;
        X[n] = tdi_td->X[m];
        Y[n] = tdi_td->Y[m];
        Z[n] = tdi_td->Z[m];
        A[n] = tdi_td->A[m];
        E[n] = tdi_td->E[m];
        T[n] = tdi_td->T[m];
    }
    
    /* Tukey window time-domain TDI channels tdi_td */
    double alpha = (2.0*TUKEY_FILTER_LENGTH/Tobs);
    
    tukey(X, alpha, NFFT);
    tukey(Y, alpha, NFFT);
    tukey(Z, alpha, NFFT);
    tukey(A, alpha, NFFT);
    tukey(E, alpha, NFFT);
    tukey(T, alpha, NFFT);
    
    /* Fourier transform time-domain TDI channels */
    gsl_fft_real_radix2_transform(X, 1, NFFT);
    gsl_fft_real_radix2_transform(Y, 1, NFFT);
    gsl_fft_real_radix2_transform(Z, 1, NFFT);
    gsl_fft_real_radix2_transform(A, 1, NFFT);
    gsl_fft_real_radix2_transform(E, 1, NFFT);
    gsl_fft_real_radix2_transform(T, 1, NFFT);
    
    /* Normalize FD data */
    double fft_norm = 2./sqrt((double)(NFFT));   // Fourier scaling
    for(int n=0; n<NFFT; n++)
    {
        X[n] *= fft_norm;
        Y[n] *= fft_norm;
        Z[n] *= fft_norm;
        A[n] *= fft_norm;
        E[n] *= fft_norm;
        T[n] *= fft_norm;
    }
    
    /* Allocate and fill data->tdi structure */
    alloc_tdi(tdi, NFFT/2, N_TDI_CHANNELS);
    tdi->delta = 1./Tobs;
    
    /* unpack GSL-formatted arrays to the way GBMCMC expects them */
    for(int n=0; n<NFFT/2; n++)
    {
        int re = 2*n;
        int im = re+1;
        
        /* real part */
        tdi->X[re] = X[n];
        tdi->Y[re] = Y[n];
        tdi->Z[re] = Z[n];
        tdi->A[re] = A[n];
        tdi->E[re] = E[n];
        tdi->T[re] = T[n];
        
        /* imaginary part*/
        if(n>0) //DC part is zero (initialized in alloc_tdi())
        {
            tdi->X[im] = X[NFFT-n];
            tdi->Y[im] = Y[NFFT-n];
            tdi->Z[im] = Z[NFFT-n];
            tdi->A[im] = A[NFFT-n];
            tdi->E[im] = E[NFFT-n];
            tdi->T[im] = T[NFFT-n];
        }
    }
    
    /* Free memory */
    free_tdi(tdi_td);
    free(X);
    free(Y);
    free(Z);
    free(A);
    free(E);
    free(T);
    
}

void GalacticBinaryReadASCII(struct Data *data, struct TDI *tdi)
{
    double f;
    double junk;
    
    FILE *fptr = fopen(data->fileName,"r");
    
    //count number of samples
    int Nsamples = 0;
    while(!feof(fptr))
    {
        int check = fscanf(fptr,"%lg %lg %lg %lg %lg",&f,&junk,&junk,&junk,&junk);
        if(!check)
        {
            fprintf(stderr,"Error reading %s\n",data->fileName);
            exit(1);
        }
        Nsamples++;
    }
    rewind(fptr);
    Nsamples--;
    
    //load full dataset into TDI structure
    alloc_tdi(tdi, Nsamples, 3);
    
    for(int n=0; n<Nsamples; n++)
    {
        int check = fscanf(fptr,"%lg %lg %lg %lg %lg",&f,&tdi->A[2*n],&tdi->A[2*n+1],&tdi->E[2*n],&tdi->E[2*n+1]);
        if(!check)
        {
            fprintf(stderr,"Error reading %s\n",data->fileName);
            exit(1);
        }
        
    }
    fclose(fptr);
}

void GalacticBinaryReadData(struct Data *data, struct Orbit *orbit, struct Flags *flags)
{
    fprintf(stdout,"\n==== GalacticBinaryReadData ====\n");
    
    struct TDI *tdi = data->tdi[0];
    
    
    /* load full dataset */
    struct TDI *tdi_full = malloc(sizeof(struct TDI));
    if(flags->hdf5Data)
        GalacticBinaryReadHDF5(data,tdi_full);
    else
        GalacticBinaryReadASCII(data,tdi_full);
    
    
    /* select frequency segment */
    
    //get max and min samples
    data->fmax = data->fmin + data->N/data->T;
    data->qmin = (int)(data->fmin*data->T);
    data->qmax = data->qmin+data->N;
    
    //store frequency segment in TDI structure
    for(int n=0; n<2*data->N; n++)
    {
        int m = data->qmin*2+n;
        tdi->A[n] = tdi_full->A[m];
        tdi->E[n] = tdi_full->E[m];
    }
    
    //Get noise spectrum for data segment
    GalacticBinaryGetNoiseModel(data,orbit,flags);
    
    //Add Gaussian noise to injection
    if(flags->simNoise) GalacticBinaryAddNoise(data,tdi);
    
    //print various data products for plotting
    print_data(data, tdi, 0);
    
    //free memory
    free_tdi(tdi_full);
}

void GalacticBinaryGetNoiseModel(struct Data *data, struct Orbit *orbit, struct Flags *flags)
{
    for(int n=0; n<data->N; n++)
    {
        double f = data->fmin + (double)(n)/data->T;
        if(strcmp(data->format,"phase")==0)
        {
            data->noise[0]->SnA[n] = AEnoise(orbit->L, orbit->fstar, f);
            data->noise[0]->SnE[n] = AEnoise(orbit->L, orbit->fstar, f);
            if(flags->confNoise)
            {
                data->noise[0]->SnA[n] += GBnoise(data->T,f);
                data->noise[0]->SnE[n] += GBnoise(data->T,f);
            }
        }
        else if(strcmp(data->format,"frequency")==0)
        {
            data->noise[0]->SnA[n] = AEnoise_FF(orbit->L, orbit->fstar, f);
            data->noise[0]->SnE[n] = AEnoise_FF(orbit->L, orbit->fstar, f);
            if(flags->confNoise)
            {
                data->noise[0]->SnA[n] += GBnoise_FF(data->T, orbit->fstar, f);
                data->noise[0]->SnE[n] += GBnoise_FF(data->T, orbit->fstar, f);
            }
        }
        else
        {
            fprintf(stderr,"Unsupported data format %s",data->format);
            exit(1);
        }
    }
}

void GalacticBinaryAddNoise(struct Data *data, struct TDI *tdi)
{
    
    printf("   ...adding Gaussian noise realization\n");
    
    //set RNG for noise
    const gsl_rng_type *T = gsl_rng_default;
    gsl_rng *r = gsl_rng_alloc(T);
    gsl_rng_env_setup();
    gsl_rng_set (r, data->nseed);
    
    for(int n=0; n<data->N; n++)
    {
        tdi->A[2*n]   += gsl_ran_gaussian (r, sqrt(data->noise[0]->SnA[n]/2.));
        tdi->A[2*n+1] += gsl_ran_gaussian (r, sqrt(data->noise[0]->SnA[n]/2.));
        
        tdi->E[2*n]   += gsl_ran_gaussian (r, sqrt(data->noise[0]->SnE[n]/2.));
        tdi->E[2*n+1] += gsl_ran_gaussian (r, sqrt(data->noise[0]->SnE[n]/2.));
    }
    
    gsl_rng_free(r);
}

void GalacticBinarySimulateData(struct Data *data)
{
}

void GalacticBinaryInjectVerificationSource(struct Data *data, struct Orbit *orbit, struct Flags *flags)
{
    //TODO: support Michelson-only injection
    fprintf(stdout,"\n==== GalacticBinaryInjectVerificationSource ====\n");
    
    FILE *fptr;
    
    /* Get injection parameters */
    double f0,dfdt,costheta,phi,m1,m2,D; //read from injection file
    double cosi,phi0,psi;                //drawn from prior
    double Mc,amp;                       //calculated
    
    FILE *injectionFile;
    FILE *paramFile;
    char filename[MAXSTRINGSIZE];
    char header[MAXSTRINGSIZE];
    
    for(int ii = 0; ii<flags->NINJ; ii++)
    {
        
        injectionFile = fopen(flags->injFile[ii],"r");
        if(!injectionFile)
            fprintf(stderr,"Missing injection file %s\n",flags->injFile[ii]);
        else
            fprintf(stdout,"Injecting verification binary %s  (%i/%i)\n",flags->injFile[ii],ii+1, flags->NINJ);
        
        //strip off header
        char *line = fgets(header, MAXSTRINGSIZE, injectionFile);
        if(line==NULL)
        {
            fprintf(stderr,"Error reading %s\n",flags->injFile[ii]);
            exit(1);
        }
        
        //parse injection parameters
        int check = fscanf(injectionFile,"%lg %lg %lg %lg %lg %lg %lg %lg",&f0,&dfdt,&costheta,&phi,&m1,&m2,&cosi,&D);
        if(!check)
        {
            fprintf(stderr,"Error reading %s\n",flags->injFile[ii]);
            exit(1);
        }
        
        
        //incoming distance in kpc, function expects pc
        D *= 1000.0;
        
        
        //draw extrinsic parameters
        
        //set RNG for injection
        const gsl_rng_type *T = gsl_rng_default;
        gsl_rng *r = gsl_rng_alloc(T);
        gsl_rng_env_setup();
        gsl_rng_set (r, data->iseed);
        
        //TODO: support for verification binary priors
        //cosi = -1.0 + gsl_rng_uniform(r)*2.0;
        phi0 = gsl_rng_uniform(r)*M_PI*2.;
        psi  = gsl_rng_uniform(r)*M_PI/4.;
        
        
        //compute derived parameters
        Mc  = chirpmass(m1,m2);
        amp = galactic_binary_Amp(Mc, f0, D, data->T);
        
        for(int jj=0; jj<flags->NT; jj++)
        {
            
            struct TDI *tdi = data->tdi[jj];
            
            //set bandwidth of data segment centered on injection
            data->fmin = f0 - (data->N/2)/data->T;
            data->fmax = f0 + (data->N/2)/data->T;
            data->qmin = (int)(data->fmin*data->T);
            data->qmax = data->qmin+data->N;
            
            //recompute fmin and fmax so they align with a bin
            data->fmin = data->qmin/data->T;
            data->fmax = data->qmax/data->T;
            
            if(jj==0)fprintf(stdout,"Frequency bins for segment [%i,%i]\n",data->qmin,data->qmax);
            fprintf(stdout,"   ...start time  %g\n",data->t0[jj]);
            
            
            struct Source *inj = data->inj;
            
            for(int n=0; n<2*data->N; n++)
            {
                inj->tdi->A[n] = 0.0;
                inj->tdi->E[n] = 0.0;
                inj->tdi->X[n] = 0.0;
            }
            
            //map parameters to vector
            inj->f0       = f0;
            inj->dfdt     = dfdt;
            inj->costheta = costheta;
            inj->phi      = phi;
            inj->amp      = amp;
            inj->cosi     = cosi;
            inj->phi0     = phi0;
            inj->psi      = psi;
            map_params_to_array(inj, inj->params, data->T);
            
            //save parameters to file
            sprintf(filename,"injection_parameters_%i_%i.dat",ii,jj);
            paramFile=fopen(filename,"w");
            fprintf(paramFile,"%lg ",data->t0[jj]);
            print_source_params(data, inj, paramFile);
            fprintf(paramFile,"\n");
            fclose(paramFile);
            
            //Book-keeping of injection time-frequency volume
            galactic_binary_alignment(orbit, data, inj);
            
            //Simulate gravitational wave signal
            //double t0 = data->t0 + jj*(data->T + data->tgap);
            galactic_binary(orbit, data->format, data->T, data->t0[jj], inj->params, 8, inj->tdi->X, inj->tdi->A, inj->tdi->E, inj->BW, 2);
            
            //Add waveform to data TDI channels
            for(int n=0; n<inj->BW; n++)
            {
                int i = n+inj->imin;
                
                tdi->X[2*i]   = inj->tdi->X[2*n];
                tdi->X[2*i+1] = inj->tdi->X[2*n+1];
                
                tdi->A[2*i]   = inj->tdi->A[2*n];
                tdi->A[2*i+1] = inj->tdi->A[2*n+1];
                
                tdi->E[2*i]   = inj->tdi->E[2*n];
                tdi->E[2*i+1] = inj->tdi->E[2*n+1];
            }
            
            sprintf(filename,"data/waveform_injection_%i_%i.dat",ii,jj);
            fptr=fopen(filename,"w");
            for(int i=0; i<data->N; i++)
            {
                double f = (double)(i+data->qmin)/data->T;
                fprintf(fptr,"%lg %lg %lg %lg %lg",
                        f,
                        tdi->A[2*i],tdi->A[2*i+1],
                        tdi->E[2*i],tdi->E[2*i+1]);
                fprintf(fptr,"\n");
            }
            fclose(fptr);
            
            sprintf(filename,"data/power_injection_%i_%i.dat",ii,jj);
            fptr=fopen(filename,"w");
            for(int i=0; i<data->N; i++)
            {
                double f = (double)(i+data->qmin)/data->T;
                fprintf(fptr,"%.12g %lg %lg ",
                        f,
                        tdi->A[2*i]*tdi->A[2*i]+tdi->A[2*i+1]*tdi->A[2*i+1],
                        tdi->E[2*i]*tdi->E[2*i]+tdi->E[2*i+1]*tdi->E[2*i+1]);
                fprintf(fptr,"\n");
            }
            fclose(fptr);
            
            //Get noise spectrum for data segment
            GalacticBinaryGetNoiseModel(data,orbit,flags);
            
            //Get injected SNR
            fprintf(stdout,"   ...injected SNR=%g\n",snr(inj, data->noise[jj]));
            
            //Add Gaussian noise to injection
            if(flags->simNoise)
            {
                data->nseed+=jj;
                GalacticBinaryAddNoise(data,tdi);
            }
            
            //Compute fisher information matrix of injection
            printf("   ...computing Fisher Information Matrix of injection\n");
            
            galactic_binary_fisher(orbit, data, inj, data->noise[jj]);
            
            /*
             printf("\n Fisher Matrix:\n");
             for(int i=0; i<8; i++)
             {
             fprintf(stdout," ");
             for(int j=0; j<8; j++)
             {
             if(inj->fisher_matrix[i][j]<0)fprintf(stdout,"%.2e ", inj->fisher_matrix[i][j]);
             else                          fprintf(stdout,"+%.2e ",inj->fisher_matrix[i][j]);
             }
             fprintf(stdout,"\n");
             }
             
             printf("\n Fisher std. errors:\n");
             for(int j=0; j<8; j++)  fprintf(stdout," %.4e\n", 1./sqrt(inj->fisher_evalue[j]));
             */
            
            
            sprintf(filename,"data/power_data_%i_%i.dat",ii,jj);
            fptr=fopen(filename,"w");
            
            for(int i=0; i<data->N; i++)
            {
                double f = (double)(i+data->qmin)/data->T;
                fprintf(fptr,"%.12g %lg %lg ",
                        f,
                        tdi->A[2*i]*tdi->A[2*i]+tdi->A[2*i+1]*tdi->A[2*i+1],
                        tdi->E[2*i]*tdi->E[2*i]+tdi->E[2*i+1]*tdi->E[2*i+1]);
                fprintf(fptr,"\n");
            }
            fclose(fptr);
            fclose(injectionFile);
            
            sprintf(filename,"data/data_%i_%i.dat",ii,jj);
            fptr=fopen(filename,"w");
            
            for(int i=0; i<data->N; i++)
            {
                double f = (double)(i+data->qmin)/data->T;
                fprintf(fptr,"%.12g %lg %lg %lg %lg",
                        f,
                        tdi->A[2*i],tdi->A[2*i+1],
                        tdi->E[2*i],tdi->E[2*i+1]);
                fprintf(fptr,"\n");
            }
            fclose(fptr);
            
            //TODO: fill X vectors with A channel for now
            for(int n=0; n<data->N; n++)
            {
                tdi->X[2*n]   = tdi->A[2*n];
                tdi->X[2*n+1] = tdi->A[2*n+1];
            }
            
        }//end jj loop over time segments
        gsl_rng_free(r);
    }
    
    fprintf(stdout,"================================================\n\n");
}
void GalacticBinaryInjectSimulatedSource(struct Data *data, struct Orbit *orbit, struct Flags *flags)
{
    //TODO: support Michelson-only injection
    //  fprintf(stdout,"\n==== GalacticBinaryInjectSimulatedSource ====\n");
    //  fprintf(stdout,"==== !!!!!!!!!!!!!WARNING!!!!!!!!!!!!!!!!====\n");
    //  fprintf(stdout,"==== HACKED CODE TO READ fddot FROM FILE ====\n");
    //  fprintf(stdout,"====    INCOMPATABLE WITH EVERYTHING     ====\n");
    FILE *fptr;
    
    /* Get injection parameters */
    //double f0,dfdt,theta,phi,amp,iota,phi0,psi,fddot;//read from injection file
    double f0,dfdt,theta,phi,amp,iota,phi0,psi;//read from injection file
    
    FILE *injectionFile;
    FILE *paramFile;
    char filename[1024];
    
    if(flags->NINJ==0)
    {
        data->fmax = data->fmin + data->N/data->T;
        data->qmin = (int)(data->fmin*data->T);
        data->qmax = data->qmin+data->N;
    }
    
    for(int ii = 0; ii<flags->NINJ; ii++)
    {
        
        injectionFile = fopen(flags->injFile[ii],"r");
        if(!injectionFile)
            fprintf(stderr,"Missing injection file %s\n",flags->injFile[ii]);
        else
            fprintf(stdout,"Injecting simulated source %s  (%i/%i)\n",flags->injFile[ii],ii+1, flags->NINJ);
        
        
        //count sources in file
        int N=0;
        while(!feof(injectionFile))
        {
            int check = fscanf(injectionFile,"%lg %lg %lg %lg %lg %lg %lg %lg",&f0,&dfdt,&theta,&phi,&amp,&iota,&psi,&phi0);
            if(!check)
            {
                fprintf(stderr,"Error reading %s\n",flags->injFile[ii]);
                exit(1);
            }
            N++;
        }
        rewind(injectionFile);
        N--;
        
        //set RNG for injection
        const gsl_rng_type *T = gsl_rng_default;
        gsl_rng *r = gsl_rng_alloc(T);
        gsl_rng_env_setup();
        gsl_rng_set (r, data->iseed);
        
        for(int nn=0; nn<N; nn++)
        {
            int check = fscanf(injectionFile,"%lg %lg %lg %lg %lg %lg %lg %lg",&f0,&dfdt,&theta,&phi,&amp,&iota,&psi,&phi0);
            if(!check)
            {
                fprintf(stderr,"Error reading %s\n",flags->injFile[ii]);
                exit(1);
            }
            
            for(int jj=0; jj<flags->NT; jj++)
            {
                
                struct TDI *tdi = data->tdi[jj];
                
                
                //set bandwidth of data segment centered on injection
                if(nn==0)
                {
                    data->fmin = f0 - (data->N/2)/data->T;
                    data->fmax = f0 + (data->N/2)/data->T;
                    data->qmin = (int)(data->fmin*data->T);
                    data->qmax = data->qmin+data->N;
                    
                    //recompute fmin and fmax so they align with a bin
                    data->fmin = data->qmin/data->T;
                    data->fmax = data->qmax/data->T;
                    
                    if(jj==0)fprintf(stdout,"Frequency bins for segment [%i,%i]\n",data->qmin,data->qmax);
                    fprintf(stdout,"   ...start time: %g\n",data->t0[jj]);
                }
                
                
                struct Source *inj = data->inj;
                
                for(int n=0; n<2*data->N; n++)
                {
                    inj->tdi->A[n] = 0.0;
                    inj->tdi->E[n] = 0.0;
                    inj->tdi->X[n] = 0.0;
                }
                
                //map polarization angle into [0:pi], preserving relation to phi0
                if(psi>M_PI) psi  -= M_PI;
                if(phi0>PI2) phi0 -= PI2;
                
                //map parameters to vector
                inj->f0       = f0;
                inj->dfdt     = dfdt;
                inj->costheta = cos(M_PI/2. - theta);
                inj->phi      = phi;
                inj->amp      = amp;
                inj->cosi     = cos(iota);
                inj->phi0     = phi0;
                inj->psi      = psi;
                if(data->NP>8)
                    inj->d2fdt2 = 11.0/3.0*dfdt*dfdt/f0;
                //inj->d2fdt2 = fddot;
                
                map_params_to_array(inj, inj->params, data->T);
                
                //save parameters to file
                sprintf(filename,"injection_parameters_%i_%i.dat",ii,jj);
                if(nn==0)paramFile=fopen(filename,"w");
                else     paramFile=fopen(filename,"a");
                fprintf(paramFile,"%lg ",data->t0[jj]);
                print_source_params(data, inj, paramFile);
                fprintf(paramFile,"\n");
                fclose(paramFile);
                
                //Book-keeping of injection time-frequency volume
                galactic_binary_alignment(orbit, data, inj);
                
                printf("   ...bandwidth : %i\n",inj->BW);
                printf("   ...fdot      : %g\n",inj->dfdt*data->T*data->T);
                printf("   ...fddot     : %g\n",inj->d2fdt2*data->T*data->T*data->T);
                
                //Simulate gravitational wave signal
                //double t0 = data->t0 + jj*(data->T + data->tgap);
                printf("   ...t0        : %g\n",data->t0[jj]);
                galactic_binary(orbit, data->format, data->T, data->t0[jj], inj->params, data->NP, inj->tdi->X, inj->tdi->A, inj->tdi->E, inj->BW, 2);
                
                //Add waveform to data TDI channels
                for(int n=0; n<inj->BW; n++)
                {
                    int i = n+inj->imin;
                    
                    tdi->X[2*i]   += inj->tdi->X[2*n];
                    tdi->X[2*i+1] += inj->tdi->X[2*n+1];
                    
                    tdi->A[2*i]   += inj->tdi->A[2*n];
                    tdi->A[2*i+1] += inj->tdi->A[2*n+1];
                    
                    tdi->E[2*i]   += inj->tdi->E[2*n];
                    tdi->E[2*i+1] += inj->tdi->E[2*n+1];
                }
                
                sprintf(filename,"data/waveform_injection_%i_%i.dat",ii,jj);
                fptr=fopen(filename,"w");
                for(int i=0; i<data->N; i++)
                {
                    double f = (double)(i+data->qmin)/data->T;
                    fprintf(fptr,"%lg %lg %lg %lg %lg",
                            f,
                            tdi->A[2*i],tdi->A[2*i+1],
                            tdi->E[2*i],tdi->E[2*i+1]);
                    fprintf(fptr,"\n");
                }
                fclose(fptr);
                
                sprintf(filename,"data/power_injection_%i_%i.dat",ii,jj);
                fptr=fopen(filename,"w");
                for(int i=0; i<data->N; i++)
                {
                    double f = (double)(i+data->qmin)/data->T;
                    fprintf(fptr,"%.12g %lg %lg ",
                            f,
                            tdi->A[2*i]*tdi->A[2*i]+tdi->A[2*i+1]*tdi->A[2*i+1],
                            tdi->E[2*i]*tdi->E[2*i]+tdi->E[2*i+1]*tdi->E[2*i+1]);
                    fprintf(fptr,"\n");
                }
                fclose(fptr);
                
                //Get noise spectrum for data segment
                GalacticBinaryGetNoiseModel(data,orbit,flags);
                
                //Get injected SNR
                fprintf(stdout,"   ...injected SNR=%g\n",snr(inj, data->noise[jj]));
                
                //Add Gaussian noise to injection
                if(flags->simNoise && nn==0)
                {
                    data->nseed+=jj;
                    GalacticBinaryAddNoise(data,tdi);
                }
                
                //Compute fisher information matrix of injection
                printf("   ...computing Fisher Information Matrix of injection\n");
                
                galactic_binary_fisher(orbit, data, inj, data->noise[jj]);
                
                
                printf("\n Fisher Matrix:\n");
                for(int i=0; i<data->NP; i++)
                {
                    fprintf(stdout," ");
                    for(int j=0; j<data->NP; j++)
                    {
                        if(inj->fisher_matrix[i][j]<0)fprintf(stdout,"%.2e ", inj->fisher_matrix[i][j]);
                        else                          fprintf(stdout,"+%.2e ",inj->fisher_matrix[i][j]);
                    }
                    fprintf(stdout,"\n");
                }
                
                printf("\n Fisher std. errors:\n");
                for(int j=0; j<data->NP; j++)  fprintf(stdout," %.4e\n", sqrt(inj->fisher_matrix[j][j]));
                
                
                
                sprintf(filename,"data/power_data_%i_%i.dat",ii,jj);
                fptr=fopen(filename,"w");
                
                for(int i=0; i<data->N; i++)
                {
                    double f = (double)(i+data->qmin)/data->T;
                    fprintf(fptr,"%.12g %lg %lg ",
                            f,
                            tdi->A[2*i]*tdi->A[2*i]+tdi->A[2*i+1]*tdi->A[2*i+1],
                            tdi->E[2*i]*tdi->E[2*i]+tdi->E[2*i+1]*tdi->E[2*i+1]);
                    fprintf(fptr,"\n");
                }
                fclose(fptr);
                
                sprintf(filename,"data/data_%i_%i.dat",ii,jj);
                fptr=fopen(filename,"w");
                
                for(int i=0; i<data->N; i++)
                {
                    double f = (double)(i+data->qmin)/data->T;
                    fprintf(fptr,"%.12g %lg %lg %lg %lg",
                            f,
                            tdi->A[2*i],tdi->A[2*i+1],
                            tdi->E[2*i],tdi->E[2*i+1]);
                    fprintf(fptr,"\n");
                }
                fclose(fptr);
                
                //TODO: fill X vectors with A channel for now
                for(int n=0; n<data->N; n++)
                {
                    tdi->X[2*n]   = tdi->A[2*n];
                    tdi->X[2*n+1] = tdi->A[2*n+1];
                }
                
            }//end jj loop over segments
        }//end nn loop over sources in file
        fclose(injectionFile);
        gsl_rng_free(r);
    }
    
    fprintf(stdout,"================================================\n\n");
}

void GalacticBinaryCatalogSNR(struct Data *data, struct Orbit *orbit, struct Flags *flags)
{
    fprintf(stdout,"\n==== GalacticBinaryInjectSimulatedSource ====\n");
    
    /* Get injection parameters */
    double f0,dfdt,theta,phi,amp,iota,psi,phi0; //read from injection file
    
    FILE *injectionFile = fopen(flags->injFile[0],"r");
    if(!injectionFile)
        fprintf(stderr,"Missing catalog file %s\n",flags->injFile[0]);
    else
        fprintf(stdout,"Simulateing binary catalog %s\n",flags->injFile[0]);
    
    //count sources in file
    int N=0;
    while(!feof(injectionFile))
    {
        int check = fscanf(injectionFile,"%lg %lg %lg %lg %lg %lg %lg %lg",&f0,&dfdt,&theta,&phi,&amp,&iota,&psi,&phi0);
        if(!check)
        {
            fprintf(stderr,"Error reading %s\n",flags->injFile[0]);
            exit(1);
        }
        N++;
    }
    rewind(injectionFile);
    N--;
    
    fprintf(stdout,"Found %i sources in %s\n",N,flags->injFile[0]);
    
    FILE *outfile = fopen("snr.dat","w");
    for(int n=0; n<N; n++)
    {
        
        int check = fscanf(injectionFile,"%lg %lg %lg %lg %lg %lg %lg %lg",&f0,&dfdt,&theta,&phi,&amp,&iota,&psi,&phi0);
        if(!check)
        {
            fprintf(stderr,"Error reading %s\n",flags->injFile[0]);
            exit(1);
        }
        
        //set bandwidth of data segment centered on injection
        data->fmin = f0 - (data->N/2)/data->T;
        data->fmax = f0 + (data->N/2)/data->T;
        data->qmin = (int)(data->fmin*data->T);
        data->qmax = data->qmin+data->N;
        
        struct Source *inj = data->inj;
        
        for(int n=0; n<2*data->N; n++)
        {
            inj->tdi->A[n] = 0.0;
            inj->tdi->E[n] = 0.0;
            inj->tdi->X[n] = 0.0;
        }
        
        //map parameters to vector
        inj->f0       = f0;
        inj->dfdt     = dfdt;
        inj->costheta = cos(M_PI/2. - theta);
        inj->phi      = phi;
        inj->amp      = amp;
        inj->cosi     = cos(iota);
        inj->phi0     = phi0;
        inj->psi      = psi;
        
        map_params_to_array(inj, inj->params, data->T);
        
        //Book-keeping of injection time-frequency volume
        galactic_binary_alignment(orbit, data, inj);
        
        //Simulate gravitational wave signal
        double t0 = data->t0[0];
        galactic_binary(orbit, data->format, data->T, t0, inj->params, 8, inj->tdi->X, inj->tdi->A, inj->tdi->E, inj->BW, 2);
        
        //Get noise spectrum for data segment
        GalacticBinaryGetNoiseModel(data,orbit,flags);
        
        //Get injected SNR
        double SNR = snr(inj, data->noise[0]);
        double Mc  = galactic_binary_Mc(f0, dfdt, data->T);
        double dL  = galactic_binary_dL(f0, dfdt, amp);
        
        fprintf(outfile,"%g %g %g %g %g %g %g %g %g\n",f0,dfdt,amp,cos(iota),Mc,dL,cos(M_PI/2 - theta),phi,SNR);
    }
    
    fclose(injectionFile);
    
    fprintf(stdout,"================================================\n\n");
}

void GalacticBinaryCleanEdges(struct Data *data, struct Orbit *orbit, struct Flags *flags)
{
    //parse file
    FILE *catalog_file = fopen(flags->catalogFile,"r");
    
    //count number of sources
    
    struct Source *catalog_entry = NULL;
    catalog_entry = malloc(sizeof(struct Source));
    alloc_source(catalog_entry, data->N, data->Nchannel, data->NP);
    
    
    int Nsource = 0;
    while(!feof(catalog_file))
    {
        scan_source_params(data, catalog_entry, catalog_file);
        Nsource++;
    }
    Nsource--;
    rewind(catalog_file);
    
    
    //allocate model & source structures
    struct Model *catalog = malloc(sizeof(struct Model));
    alloc_model(catalog, Nsource, data->N, data->Nchannel, data->NP, data->NT);
    
    catalog->Nlive = 0;
    for(int n=0; n<Nsource; n++)
    {
        scan_source_params(data, catalog_entry, catalog_file);
        
        //find where the source fits in the measurement band
        galactic_binary_alignment(orbit, data, catalog_entry);
        double q0   = catalog_entry->params[0];
        double qmax = catalog_entry->params[0] + catalog_entry->BW/2;
        double qmin = catalog_entry->params[0] - catalog_entry->BW/2;
        
        
        /*
         only substract tails of sources outside of window that leak in.
         this means we are redundantly fitting sources in the overlap region, but that's better than fitting bad residuals
         */
        if( (q0 < data->qmin && qmax > data->qmin) || (q0 > data->qmax && qmin < data->qmax) )
        {
            copy_source(catalog_entry, catalog->source[catalog->Nlive]);
            catalog->Nlive++;
        }
    }
    
    
    //for binaries in padded region, compute model
    generate_signal_model(orbit, data, catalog, -1);
    
    //remove from data
    for(int n=0; n<catalog->NT; n++)
    {
        
        for(int i=0; i<data->N*2; i++)
        {
            data->tdi[n]->X[i] -= catalog->tdi[n]->X[i];
            data->tdi[n]->A[i] -= catalog->tdi[n]->A[i];
            data->tdi[n]->E[i] -= catalog->tdi[n]->E[i];
        }
    }
    
    char filename[128];
    sprintf(filename,"data/power_residual_%i_%i.dat",0,0);
    FILE *fptr=fopen(filename,"w");
    
    for(int i=0; i<data->N; i++)
    {
        double f = (double)(i+data->qmin)/data->T;
        fprintf(fptr,"%.12g %lg %lg ",
                f,
                data->tdi[0]->A[2*i]*data->tdi[0]->A[2*i]+data->tdi[0]->A[2*i+1]*data->tdi[0]->A[2*i+1],
                data->tdi[0]->E[2*i]*data->tdi[0]->E[2*i]+data->tdi[0]->E[2*i+1]*data->tdi[0]->E[2*i+1]);
        fprintf(fptr,"\n");
    }
    fclose(fptr);
    
    //clean up after yourself
    fclose(catalog_file);
    free_model(catalog);
    free_source(catalog_entry);
}
