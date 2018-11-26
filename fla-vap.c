/**********************************************************************
Coupled ANSYS UDFs for Fully Lagrangian Approach (FLA) foro spray modelling with the heat and mass modelling for a single component droplet
Has been tested with ANSYS Fluent 17.2

A user can select from water, n-dodecane, and iso-octane droplets.

The references for physical properties are in the code for each of the fluids
For more detail on the FLA, see
1. A. N. Osiptsov, Lagrangian modelling of dust admixture in gas flows, Astrophysics and Space Science 274 (1-2) (2000) 377{386. doi:10.1023/200 A:1026557603451.
2. D. P. Healy, J. B. Young, Full lagrangian methods for calculating particle concentration elds in dilute gas-particle flows, Proceedings of the Royal Society of London A: Mathematical, Physical and Engineering Sciences 195 461 (2059) (2005) 2197{2225. doi:10.1098/rspa.2004.1413.

If you use our software, please cite the following:
Timur S. Zaripov, Oyuna Rybdylova, Sergei S. Sazhin, A model for heating and evaporation of a droplet cloud and its implementation into ANSYS Fluent, International Communications in Heat and Mass Transfer, Volume 97, 2018, Pages 85-91, ISSN 0735-1933,
https://doi.org/10.1016/j.icheatmasstransfer.2018.06.007.

Copyright (C) 2018 Oyuna Rybdylova, Timur Zaripov - All Rights Reserved
You may use, distribute and modify this code under the terms of the MIT license
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
***********************************************************************/
#include "udf.h"
#include <time.h>

// user settings
#define DODECANE
#undef WATER
#undef ISOOCTANE
#define FLA_AXISYM

#define DPM_DT (1.e-4)

#define BM_MAX 1.E20
#define BM_MIN -0.99999
#define ACCURACY 1.e-6
#define PI 3.1415926535897932384626433832795
#define N_Lambda 44 // number of terms in the series
#define N_INT 100 // number of layers inside a droplet
#define Delta_R 0.01 // = 1/N_INT

// 136 DPM_USER_REALs have to be enabled in ANSYS Fluent.
// there is a check in Heat and Mass transfer on the number of components
#define NCOMPONENTS 1
#define VAP_END (116)
#define FLA_OFFSET (VAP_END + 4) // DPM_USER_REALs are required by VPA part
#define FLA_N_SCAL (16)          // DPM_USER_REALs required by FLA part

#define P_VAP_dhdt(p)         P_USER_REAL(p, VAP_END)
#define P_VAP_dhdt_scaled(p)  P_USER_REAL(p, VAP_END + 1)
#define P_VAP_dmdt(p)         P_USER_REAL(p, VAP_END + 2)
#define P_VAP_dmdt_scaled(p)  P_USER_REAL(p, VAP_END + 3)

// BEGIN FLA defines
// Number of equations in fla_dydt().
#define N_EQ (8)

//
// IMPORTANT
// DO NOT change the order of the J, W components. It will screw dydt().
//
// Components of the jacobian and the helper matrix w (du_i/dx0_j):
#define J11(p)      P_USER_REAL(p, FLA_OFFSET + 0) 
#define J12(p)      P_USER_REAL(p, FLA_OFFSET + 1)
#define J21(p)      P_USER_REAL(p, FLA_OFFSET + 2)
#define J22(p)      P_USER_REAL(p, FLA_OFFSET + 3)
#define W11(p)      P_USER_REAL(p, FLA_OFFSET + 4)
#define W12(p)      P_USER_REAL(p, FLA_OFFSET + 5)
#define W21(p)      P_USER_REAL(p, FLA_OFFSET + 6)
#define W22(p)      P_USER_REAL(p, FLA_OFFSET + 7)
#define J_DET(p)    P_USER_REAL(p, FLA_OFFSET + 8)  // jacobian determinant
#define N_P(p)      P_USER_REAL(p, FLA_OFFSET + 9)  // number density.
#define N_J_SIGN(p) P_USER_REAL(p, FLA_OFFSET + 10) // count of j sign changes.
#define BETA(p)     P_USER_REAL(p, FLA_OFFSET + 11) // 1/tau
#define R_0(p)      P_USER_REAL(p, FLA_OFFSET + 12) // r_0 for axisymetric case
// Values related to analytical solution:
// #define A_J_DET(p)  P_USER_REAL(p, FLA_OFFSET + 12)  // jacobian determinant
// #define A_N_P(p)    P_USER_REAL(p, FLA_OFFSET + 13)  // number density.
// #define A_U_P(p)    P_USER_REAL(p, FLA_OFFSET + 14)  // velocity x-component
// #define A_V_P(p)    P_USER_REAL(p, FLA_OFFSET + 15)  // velocity y-component
// END FLA defines 

#ifdef WATER
// Carl L. Yaws-Thermophysical Properties of Chemicals and Hydrocarbons-William 
// Andrew (2008)

// Incropera FP, DeWitt DP. Introduction to Heat Transfer. Fourth Edition:
// John Wiley & Sons, 2002.
real h2o_mw = 18.0;
real T_cr_h2o = 647.13;
real T_b_h2o = 373.15;
real omega_h2o = 0.3449;
real P_cr_h2o = 220.55e+5;
// water
real get_vapour_saturation_pressure (real T)
{
    real Tr = T / T_cr_h2o;
    real tau = 1 - Tr;
    real f0, f1, f2;
    if (T > 0.99*T_cr_h2o) { Tr = 0.99; tau = 0.01; }
    f0 = (-5.97616*tau + 1.29874 * pow(tau, 1.5) - 0.60394 * pow(tau, 2.5) - 1.06841 * pow(tau, 5.0)) / Tr;
    f1 = (-5.03365*tau + 1.11505 * pow(tau, 1.5) - 5.41217 * pow(tau, 2.5) - 7.46628 * pow(tau, 5.0)) / Tr;
    f2 = (-0.64771*tau + 2.41539 * pow(tau, 1.5) - 4.26979 * pow(tau, 2.5) + 3.25259 * pow(tau, 5.0)) / Tr;
    return exp(f0 + f1 * omega_h2o + f2 * omega_h2o* omega_h2o)*P_cr_h2o;
}

// water
real get_vapour_c_p(real T)
{ 
    return (-5.9796e-9*T*T*T + 1.7437e-5*T*T - 3.2463e-3*T + 33.174) / h2o_mw*1.e+3;  //water
}

// water
real get_vapour_binary_diffusivity(real p, real T)
{
    real Mv = 0.e-15;
    real Mva = 0.e-15;
    real T_n;
    real Omega_D;
    real sigmava, SQMva, sigma_v, e_v;

    sigma_v = 0.e-15;
    e_v = 0.e-15;

    Mva = 2.0 / (1 / h2o_mw + 1 / 28.967); //air
    SQMva = sqrt(Mva);
    sigmava = 0.5*(2.641 + 3.711); //air
    T_n = T / sqrt(78.6*809.1); //air
    Omega_D = 1.06036*pow(T_n, -0.1561) + 0.193*exp(-0.47635*T_n) + 1.03587*exp(-1.52996*T_n) + 1.76474*exp(-3.89411*T_n);
    return (3.03 - 0.98 / SQMva) / (p * SQMva * (sigmava * sigmava) * Omega_D)*1.e-2*pow(T, 1.5);  //wilke and lee
}

// water
real get_liquid_latent_heat(real T)
{
    real L_eff = 54.0*pow(1 - T / T_cr_h2o, 0.34) / h2o_mw * 1.e6;
    if (T>0.99*T_cr_h2o) {
        L_eff = 54.0*pow(0.01, 0.34) / h2o_mw * 1.e6;
    }
    return L_eff;
}

// water
real get_liquid_density(real T)
{
    return 1.0 / 1.058*1.e+3;
}    

// water
real get_liquid_visc(real T)
{ 
    return pow(10.0, -11.6225 + 1.949e+3 / T + 2.1641e-2*T - 1.5990e-5*T*T) * 1.e-3;
}

// water
real get_liquid_k(real T)
{ 
    return 686.e-3;
}

real get_liquid_c_p(real T)
{ 
    return 1000.0*4239.0;
}

#endif // water

#ifdef DODECANE
/* Dodecane saturation pressure */
// Abramzon, B. and S. Sazhin, Convective vaporization of a fuel droplet with 
// thermal radiation absorption. Fuel, 2006. 85(1): p. 32-46.
real get_vapour_saturation_pressure (real T)
{
    real psat;
    psat = exp(8.1948 - 7.8099*(300.0 / T) - 9.0098*(300.0 / T)*(300.0 / T))*1.e5;
    if (T > 0.99*659.0) {
        psat = psat*exp(15.0*(T / 0.99 / 659.0 - 1.0));
    }
    return psat;
}

// n-dodecane vapour 
// Abramzon, B. and S. Sazhin, Convective vaporization of a fuel droplet with 
// thermal radiation absorption. Fuel, 2006. 85(1): p. 32-46.
real get_vapour_c_p(real T)
{ 
    return (0.2979 + 1.4394*(T / 300.0) - 0.1351*(T / 300.0)*(T / 300.0))*1000.0;
}

real get_vapour_binary_diffusivity(real p, real T)
{
    return 0.527*pow(T / 300.0, 1.583) / p;
}

// Effective latent heat for n-dodecane vapour
// Abramzon, B. and S. Sazhin, Convective vaporization of a fuel droplet with 
// thermal radiation absorption. Fuel, 2006. 85(1): p. 32-46.
real get_liquid_latent_heat(real T)
{
    real L_eff = 37.44*pow(659.0 - T, 0.38)*1000.0;
    if (T>0.99*659.0) {
        L_eff = 37.44*pow(659.0 - 653.0, 0.38)*1000.0;
    }
    return L_eff;
}

real get_liquid_density(real T)
{
    return 744.11 - 0.771*(T - 300.0);
}    

real get_liquid_visc(real T)
{ 
    return 1.e-3*exp(2.0303*(300.0 / T)*(300.0 / T) + 1.1769*(300.0 / T) - 2.929);
}

real get_liquid_k(real T)
{ 
    return 0.1405 - 0.00022*(T - 300.0);
    //k_l *= 1000.0;  // for test purposes, to model infinitely high thermal conductivity
}

real get_liquid_c_p(real T)
{ 
    return (2.18 + 0.0041*(T - 300.0))*1000.0;
}

#endif

//=======================================================================
#ifdef ISOOCTANE
// Bruce E. Poling, John M. Prausnitz, John P. O'Connell-The Properties of Gases
// and Liquids, Fifth Edition-McGraw-Hill Professional (2000)

real T_cr_ioctane = 543.9;
real T_b_ioctane = 372.39;
real P_cr_isooctane = ( - 0.0186 * 64.0 * 8.0 + 0.459 * 64.0 - 5.924 * 8.0 + 54.071) * 100000.0;
real c8_mw = 114.23;

// Ambrose and Walton (1989).
real get_vapour_saturation_pressure (real T)
{
    real Tr = T / T_cr_ioctane;
    real tau = 1 - Tr;
    //Tr = T_cr_ioctane / T_cr_ioctane;
    real theta = T_b_ioctane / T_cr_ioctane;
    //theta = T_cr_ioctane / T_cr_ioctane;
    real f0, f1, f2;
    f0 = (-5.97616*tau + 1.29874 * pow(tau, 1.5) - 0.60394 * pow(tau, 2.5) - 1.06841 * pow(tau, 5.0)) / Tr;
    f1 = (-5.03365*tau + 1.11505 * pow(tau, 1.5) - 5.41217 * pow(tau, 2.5) - 7.46628 * pow(tau, 5.0)) / Tr;
    f2 = (-0.64771*tau + 2.41539 * pow(tau, 1.5) - 4.26979 * pow(tau, 2.5) + 3.25259 * pow(tau, 5.0)) / Tr;
    real omega1 = 0.303;
    return exp(f0 + f1 * omega1 + f2 * omega1* omega1)*P_cr_isooctane;
}

// J / (kg K)
// http://webbook.nist.gov/cgi/cbook.cgi?ID=C540841&Mask=FFF#Thermo-Gas
real get_vapour_c_p(real T)
{
    return 244.60 / c8_mw * 1000.0; // FIXME For T=400K
}

// http://www.sciencedirect.com/science/article/pii/S0016236115006080?via%3Dihub#s0020
// A = −0.0578, B = 3.0455E–4, C = 3.4265E–7.
// m^2 / s
real get_vapour_binary_diffusivity(real p, real T)
{
    return (-0.0578 + 3.0455e-4 * T + 3.4265e-7 * T * T) * 1.e-4;
}

// http://www.sciencedirect.com/science/article/pii/S0016236115006080?via%3Dihub#s0020
real get_liquid_latent_heat(real T)
{
    real L_eff = 49.32456*pow(1 - T / T_cr_ioctane, 0.382229) / c8_mw * 1.e6;
    if (T>0.99*T_cr_ioctane) {
        L_eff = 49.32456*pow(1 - 0.99, 0.382229) / c8_mw * 1.e6;
    }
    return L_eff;
}

// http://www.sciencedirect.com/science/article/pii/S0016236115006080?via%3Dihub#s0020

real get_liquid_density(real T)
{
    return 1000.0 * (- 0.000981411583995317 * 8.0* 8.0 + 0.0167403553403262 * 8.0 + 0.175683060992056)*pow(-0.000706081955526297 * 64.0 + 0.00873629109926122 * 8.0 + 0.249117016533684, -pow(1 - T / T_cr_ioctane, 0.00114456989247312 * 64.0 - 0.0174424731182795 * 8.0 + 0.343958172043011));  //octane T crit;
}    

// http://www.sciencedirect.com/science/article/pii/S0016236115006080?via%3Dihub#s0020
real get_liquid_visc(real T)
{ 
    real a = -10.2217;
    real b = 1423.586;
    real c = 0.024242;
    real d = -2.33636E-05;
    real k = a + b / T + c*T + d*T*T - 3.0;
    return pow(10.0, k);
}

// http://www.sciencedirect.com/science/article/pii/S0016236115006080?via%3Dihub#s0020
real get_liquid_k(real T)
{ 
    return 0.0035 * pow(T_b_ioctane, 1.2)*pow(c8_mw, -0.5)*pow(T_cr_ioctane, -0.167)*pow(1.0 - T / T_cr_ioctane, 0.38)*pow(T / T_cr_ioctane,-1.0/6.0);
}

// there is a typo in formula for the isooctane
// use dodecane values for now
real get_liquid_c_p(real T)
{ 
    return (2.18 + 0.0041*(T - 300.0))*1000.0;
}
#endif // isooctane


// BEGIN FLA functions 
// Convenience function. Working with P_USER_REAL is cumbersome, hence we copy
// data to local array.
int fla_read_user_real(real y[], Tracked_Particle *p)
{
    for (int i=0; i<N_EQ; i++) {
        y[i] = P_USER_REAL(p, FLA_OFFSET + i);
    }
    return 0;
}

// Convenience function. Complements fla_read_user_real().
int fla_update_user_real(const real y[], Tracked_Particle *p)
{
    for (int i=0; i<N_EQ; i++) {
        P_USER_REAL(p, FLA_OFFSET + i) = y[i];
    }
    return 0;
}

// The system of ODE for Jacobian and W components, that we solve using RK4 method.
int fla_dydt(const real y[], real f[], real tau, cell_t c, Thread *t)
{
    f[0] = y[4]; // dj11/dt = w11
    f[1] = y[5]; // dj12/dt = w12
    f[2] = y[6]; // dj21/dt = w21
    f[3] = y[7]; // dj22/dt = w22
    f[4] = (y[0]*C_DUDX(c,t) + y[2]*C_DUDY(c,t) - y[4]) / tau; // w11
    f[5] = (y[1]*C_DUDX(c,t) + y[3]*C_DUDY(c,t) - y[5]) / tau; // w12
    f[6] = (y[0]*C_DVDX(c,t) + y[2]*C_DVDY(c,t) - y[6]) / tau; // w21
    f[7] = (y[1]*C_DVDX(c,t) + y[3]*C_DVDY(c,t) - y[7]) / tau; // w22
    return EXIT_SUCCESS;
}

// 4th order Runge--Kutta method step (RK4).
int fla_rk4_step(Tracked_Particle *p, cell_t c, Thread *t)
{
    // Here we make sure, that we are using the same drag law, that is used by Fluent. 
    // See DEFINE_DPM_DRAG in the manual.
    real tau = P_RHO(p) * P_DIAM(p) * P_DIAM(p) / (p->cphase->mu * DragCoeff(p));
    BETA(p) = 1.0/tau;
    // Use the same Runge-Kutta time step as Fluent.
    real h = P_DT(p);
    real y[N_EQ];
    fla_read_user_real(y, p);
    //---------------------------------------------------------------
    // Below is the classical RK4 method.
    //---------------------------------------------------------------
    real y_tmp[N_EQ];
    real k1[N_EQ], k2[N_EQ], k3[N_EQ], k4[N_EQ];
    // k1 = f(t, y)
    fla_dydt(y, k1, tau, c, t);
    // k2 = f(t + h/2, y + k1*h/2)
    for(int i = 0; i < N_EQ; i++){
        y_tmp[i] = y[i] + k1[i] * h/2;
    }
    fla_dydt(y_tmp, k2, tau, c, t);
    // k3 = f(t + h/2, y + k2*h/2)
    for(int i = 0; i < N_EQ; i++){
        y_tmp[i] = y[i] + k2[i] * h/2;
    }
    fla_dydt(y_tmp, k3, tau, c, t);
    // k4 = f(t + h, y + k3*h)
    for(int i = 0; i < N_EQ; i++){
        y_tmp[i] = y[i] + k3[i] * h;
    }
    fla_dydt(y_tmp, k4, tau, c, t);
    // y_{i+1} = y_i + (k_1 + 2*k_2 + 2*k_3 + k_4)*h/6
    for(int i = 0; i < N_EQ; i++){
        y[i] = y[i] + (k1[i] + 2 * k2[i] + 2 * k3[i] + k4[i]) * h/6;
    }
    //---------------------------------------------------------------
    fla_update_user_real(y, p);
    return EXIT_SUCCESS;
}
// END FLA functions 

// BEGIN VAP functions 
int Lambda(real h_0, real lambda[])
{
    FILE * fout;
    int i;
    double lambda_left, lambda_right, f_left, f_right, lambda_mid, f_mid;
    double conv_crit = 1.e-8;
    double step = 1.e-7;

    for (i = 0; i < N_Lambda; i++) lambda[i] = -1.0;

    for (i = 0; i < N_Lambda; i++)
    {
        lambda_left = ((double)(i))*PI + step;
        lambda_right = (((double)(i + 1)) - 0.5)*PI - step;

        if (h_0 > 0.0)
        {
            lambda_left += 0.5*PI;
            lambda_right += PI*0.5;
        }

        f_left = lambda_left*cos(lambda_left) + h_0*sin(lambda_left);
        f_right = lambda_right*cos(lambda_right) + h_0*sin(lambda_right);
        if (f_left*f_right < 0.0)
        {
            while (lambda_right - lambda_left > conv_crit)
            {
                lambda_mid = (lambda_left + lambda_right)*0.5;
                f_mid = lambda_mid*cos(lambda_mid) + h_0*sin(lambda_mid);
                if (f_left*f_mid < 0.0)
                {
                    lambda_right = lambda_mid;
                    f_right = lambda_right*cos(lambda_right) + h_0*sin(lambda_right);
                }
                else
                {
                    lambda_left = lambda_mid;
                    f_left = lambda_left*cos(lambda_left) + h_0*sin(lambda_left);
                }
                //Message("Lambda left = %f\nLambda right = %f\n f left = %e\nf right = %e\n------------\n", lambda_left, lambda_right, f_left, f_right);
            }
            lambda[i] = lambda_left;
        }
    }

    //  Message("Lambdas are calculated\n");

    //  fout = fopen("lambda.txt", "w");
    //  for (i = 0; i < N_Lambda; i++)
    //      fprintf(fout, "%d\t%20.19f\t%e\n", i + 1, lambda[i], lambda[i] * cos(lambda[i]) + h_0*sin(lambda[i]));
    //  fclose(fout);
    //  Message("Lambdas are printed in lambda.txt\n");
    return 0;
}
// END VAP functions


/* convection diffusion controlled vaporisation model as implemented into Fluent
   p    ... tracked particle struct
   Cp   ... particle heat capacity
   hgas ... enthalpy of formation for gas species
   hvap ... vaporization enthalpy
   cvap_surf ... molar concentration of vapor at surface
   Z    ... compressibility factor
   dydt ... temperature / component mass source terms for particle 
            dydt[0] = temperature equation, 
            dydt[1+ns] = mass equation of particle species ns
   dzdt ... temperature / species mass source terms for gas phase
            dzdt->energy = convective heat transfer in Watt
            dzdt->species[n_gas] = mass source for gas species n_gas in kg/s
 */

// 4*N_component + 7 (x_i, y_i, dm_i, Mw_i, y_tot, dm_tot, D, BM, BT, diam1, diam2) + N_INT+1 
// for temperature distribution inside a droplet USER_REAL variables 
// 116 for single component n-dodecane
DEFINE_DPM_HEAT_MASS(multivap_conv_diffusion_new, p, Cp, hgas, hvap, cvap_surf, Z, dydt, dzdt)
{
    //-------------------------------------------------------------------------
    /* molecular weight of gas species */
    /* molwt_bulk is first the reciproke of the real molecular weight to avoid additional divisions */
    real molwt_bulk = 0.;
    int ns;
    Thread *t0 = P_CELL_THREAD(p);
    Material *gas_mix = THREAD_MATERIAL(DPM_THREAD(t0, p));
	cphase_state_t *c = p->cphase;  /* continuous phase struct, caching variables of the cell */

    mixture_species_loop_i(gas_mix, ns)
    {
        molwt_bulk += c->yi[ns] / solver_par.molWeight[ns]; /* c->yi[ns] contains mass fraction of fluid cell */
        //P_USER_REAL(p, 3 * nc + ns) = solver_par.molWeight[ns];
    }
    molwt_bulk = MAX(molwt_bulk, DPM_SMALL);
    molwt_bulk = 1. / molwt_bulk;
    

    /* when not using Runge Kutta solver, increase limiting time for the next integration step */
    // ANSYS stuff
    if (!p->in_rk) {
        p->limiting_time = P_DT(p)*1.01;
    }

    //-------------------------------------------------------------------------
    // Calculate molar fractions of the components at the droplet surface
    real xs_tot = 0.e-15;
    real xsM_tot = 0.e-15;
    real P_sat = 0.0; // Saturation pressure
    real x_surf =0.0; // molar fraction of component at droplet surface

    int nc = TP_N_COMPONENTS(p);
	real Tp = P_USER_REAL(p, 4 * nc + 7 + N_INT); //Dropet temperature at the surface
	if (nc != NCOMPONENTS) {
        Message("ALARM!!! nc != NCOMPONENTS.");
    }
    for (int ns = 0; ns < nc; ns++) {
        int gas_index = TP_COMPONENT_INDEX_I(p, ns); /* gas species index of vaporization */
        if (gas_index >= 0) {
            // Saturation pressure for n-dodecane vapour
            P_sat = get_vapour_saturation_pressure(Tp);
            x_surf = P_sat / c->pressure; //Saturation pressure for n-Dodecane from Abramzon&Sazhin 2006
            //above for x_surf will be modified for multicoponent droplet case
            P_USER_REAL(p, ns) = x_surf;
            xs_tot += x_surf*solver_par.molWeight[ns];
            xsM_tot += x_surf;
        }
    }
    
    xs_tot += (1.0 - xsM_tot)*28.967; //air
    
    //-------------------------------------------------------------------------
    // Calculate mass fractions of the components at the droplet surface
    real Ys = 0.e-15;
    real Y_inf = 0.e-15;
    real L_eff = 0.e-15;
    real Ys_tot = 0.e-15;
    for (int ns = 0; ns < nc; ns++) {
        /* gas species index of vaporization */
        int gas_index = TP_COMPONENT_INDEX_I(p, ns);
        if (gas_index >= 0) {
            Ys = P_USER_REAL(p, ns)* solver_par.molWeight[gas_index] / xs_tot;//!!
            Y_inf += c->yi[gas_index];
            //L_eff += Ys * p->hvap[gas_index]; // TODO Try for water
            
            L_eff += Ys*get_liquid_latent_heat(Tp);

            //Latent heat as above will be calculated separately for multicomponent droplet
            Ys_tot += Ys;
            P_USER_REAL(p, nc + ns) = Ys;
        }
    }
    L_eff = L_eff / Ys_tot;
    P_USER_REAL(p, 4 * nc) = Ys_tot;

    //-------------------------------------------------------------------------
    // Calculate Nusselt number and total evaporation rate
    real T_ref = (c->temp + 2.0*Tp) / 3.0; //Sazhin, Progress in Energy and Combustion Science 32 (2006) 162–214 
    real rho_gas_s = c->pressure / (287.01625988193461525183829875375*T_ref); // ideal gas law

    real c_p_die = get_vapour_c_p(T_ref);
      //D = 0.527*pow(T_ref / 300.0, 1.583) / c->pressure; //2015.10.29

	Material *cond_mix = P_MATERIAL(p);
	Material * cond_c = MIXTURE_COMPONENT(cond_mix, 0);
	real D = DPM_BINARY_DIFFUSIVITY(p, cond_c, Tp);
    real Sc = c->mu / (rho_gas_s * D);  //Schmidt number

    real kgas = c->tCond;
    real Re = p->Re;
    real Pr = c->sHeat * c->mu / kgas;
    //  BM = (Ys_tot - Y_inf) / (1.0 - Ys_tot);
    real BM = (Ys_tot) / (1.0 - Ys_tot); //assuming zero mass fraction in the ambient gas
    real FBM = pow(1.0 + BM, 0.7)*log(1.0 + BM) / BM;
    real Sh_Star = 2.0 + (pow(1.0 + Re*Sc, 1.0 / 3.0)*MAX(1.0, pow(Re, 0.077)) - 1.0) / FBM;
    real Sh = log(1.0 + BM)*Sh_Star;
    //Sh = log(1.0 + BM)*(2.0 + 0.6*sqrt(Re)*pow(Sc, 1.0 / 3.0));
	real Dp = P_DIAM(p);
	real Ap = DPM_AREA(Dp);
	real tot_vap_rate = Ap * D * rho_gas_s * Sh / Dp; // total evaporation rate
    
    P_USER_REAL(p, 4 * nc + 1) = tot_vap_rate;

    real BT = BM;
    real BT_i = BT;
    real dif = 1.0;
    real coef = c_p_die * rho_gas_s * D / kgas * Sh_Star;
    real phi = 0.e-15;
	real FBT, Nu_star;
    // find BT iteratively
    while (dif > ACCURACY) {
        FBT = pow(1.0 + BT, 0.7)*log(1.0 + BT) / BT;
        Nu_star = 2.0 + (pow(1.0 + Re*Pr, 1.0 / 3.0)*MAX(1.0, pow(Re, 0.077)) - 1.0) / FBT;
        phi = coef / Nu_star;
        BT = pow(1.0 + BM, phi) - 1.0;
        dif = abs(BT - BT_i);
        BT_i = BT;
    }

    real Nu = log(1.0 + BT) * Nu_star / BT; // Nusselt number

    //-------------------------------------------------------------------------
    // Temperature distribution calculations
    real T_av = P_USER_REAL(p, 4 * nc + 6);
    real Visc_l = get_liquid_visc(T_av);
    real k_l = get_liquid_k(T_av);
    real C_pl = get_liquid_c_p(T_av);

    real rel_vel = sqrt((c->V[0] - P_VEL(p)[0])*(c->V[0] - P_VEL(p)[0]) + (c->V[1] - P_VEL(p)[1])*(c->V[1] - P_VEL(p)[1]) + (c->V[2] - P_VEL(p)[2])*(c->V[2] - P_VEL(p)[2]));
    real Pe = 12.69 / 16.0*P_RHO(p)*0.5*Dp* C_pl / k_l*rel_vel*c->mu / Visc_l*pow(Re, 1.0 / 3.0) / (1.0 + BM);
    real k_eff = (1.86 + 0.86*tanh(2.225*log10(Pe / 30.0)))*k_l;  // effective thermal conductivity to take into account recirculation Abramzon B, Sirignano WA. Int J Heat Mass Transfer 1989;32:1605–18.
    if (abs(Pe) < 1.e-12) {
        k_eff = k_l;
    }

    real T_eff = c->temp - tot_vap_rate*L_eff / PI / Dp / Nu / kgas;
    real h0 = kgas*Nu*0.5 / k_eff - 1.0;
    real zeta = (h0 + 1.0)*T_eff;
    real kappa = k_eff / (C_pl*P_RHO(p)*0.25*Dp*Dp);

	real lambda[N_Lambda];
	for (int i = 0; i < N_Lambda; i++) { lambda[i] = -1.0; }
    int err = Lambda(h0, lambda);

	real series[N_Lambda];
    for (int i = 0; i < N_Lambda; i++) { series[i] = 0.e-15; }

	real I_n, b_n;
	for (int i = 0; i < N_Lambda; i++) {
        b_n = 0.5*(1.0 + h0 / (h0*h0 + lambda[i] * lambda[i]));
        I_n = 0.e-15;
        I_n = P_USER_REAL(p, 4 * nc + 7 + N_INT)*sin(lambda[i]);
        for (int j = 1; j < N_INT; j += 2) {
            I_n += 4.0 * P_USER_REAL(p, 4 * nc + 7 + j)*(((double)j)*Delta_R)*sin(lambda[i] * ((double)j)*Delta_R);
        }
        for (int j = 2; j < N_INT; j += 2) {
            I_n += 2.0 * P_USER_REAL(p, 4 * nc + 7 + j)*(((double)j)*Delta_R)*sin(lambda[i] * ((double)j)*Delta_R);
        }
        I_n = I_n*Delta_R / 3.0;
        series[i] = (I_n - sin(lambda[i]) / lambda[i] / lambda[i] * zeta)*exp(0.0 - kappa*lambda[i] * lambda[i] * P_DT(p)) / b_n;
    }

    for (int j = 0; j < N_INT + 1; j++) { P_USER_REAL(p, 4 * nc + 7 + j) = T_eff; }
    for (int i = 0; i < N_Lambda; i++)  {
        P_USER_REAL(p, 4 * nc + 7) += series[i] * lambda[i];
        for (int j = 1; j < N_INT + 1; j++) {
            P_USER_REAL(p, 4 * nc + 7 + j) += series[i] * sin(lambda[i] * ((double)j)*Delta_R) / (((double)j)*Delta_R);
        }
    }
    // Now we know temperature at each layer

    // Re-calculate droplet avarage temperature T_av
    Tp = P_USER_REAL(p, 4 * nc + 7 + N_INT);
    T_av = Tp;
    for (int j = 1; j < N_INT; j += 2) {
        T_av += 4.0 * P_USER_REAL(p, 4 * nc + 7 + j)*(((double)j)*Delta_R)*(((double)j)*Delta_R);
    }
    for (int j = 2; j < N_INT; j += 2) {
        T_av += 2.0 * P_USER_REAL(p, 4 * nc + 7 + j)*(((double)j)*Delta_R)*(((double)j)*Delta_R);
    }
    T_av = T_av*Delta_R;

    //-------------------------------------------------------------------------
    // update Fluent variables using our values
    p->state.temp = T_av;
    //p->source.htc = Nu*kgas*P_DIAM(p);
    p->source.htc = 0.e-15;  // htc - heat transfer coefficient
    
    // evaporation rates - source terms, droplet mass
    for (int ns = 0; ns < nc; ns++) {
        /* gas species index of vaporization */
        int gas_index = TP_COMPONENT_INDEX_I(p, ns);
        if (gas_index >= 0) {
            real vap_rate = P_USER_REAL(p, nc + ns) * tot_vap_rate / Ys_tot; //!!

            // ANSYS stuff
            if ((!p->in_rk) && (ABS(vap_rate)>0.)) {
                p->limiting_time = MIN(p->limiting_time, dpm_par.fractional_change_factor_mass*P_MASS(p) / vap_rate*TP_COMPONENT_I(p, ns));
            }

            P_USER_REAL(p, 2 * nc + ns) = vap_rate;
            dydt[1 + ns] -= vap_rate;

            {
                int source_index = injection_par.yi2s[gas_index];
                if (source_index >= 0) {
                    dzdt->species[source_index] += vap_rate;
                    //p->source.mtc[source_index] = c->rho * Ap * Sh_Star * D / Dp;
                    p->source.mtc[source_index] = c->rho * PI * Dp * Sh_Star * D;
                }
            }
        }
    }
    
    // Keep particle temperature independent form Fluent's source term.
    // source terms for energy equations: zero, as Temperature is calculated explicitly
    dydt[0] = 0.e-15;

    real dh_dt = Nu * kgas * Ap / Dp * (c->temp - T_av);
    dzdt->energy -= dh_dt;

    //-------------------------------------------------------------------------
    // ANSYS stuff
    real h = Nu * kgas / Dp;
	real mp = P_MASS(p);
    real convective_heating_rate = h * Ap / (mp * p->Cp);

    /* limit for higher heating rate */
    if ((!p->in_rk) && (ABS(convective_heating_rate)>DPM_SMALL)) {
            real factor = dpm_par.fractional_change_factor_heat;
            if (ABS(c->temp - Tp)>Tp) {
                factor = dpm_par.fractional_change_factor_heat*Tp / (c->temp - Tp);
            }
            p->limiting_time = MIN(p->limiting_time, factor / ABS(convective_heating_rate));
    }
    
    //-------------------------------------------------------------------------
    // update user reals
    P_USER_REAL(p, 4 * nc + 2) = BM;
    P_USER_REAL(p, 4 * nc + 3) = BT;
    P_USER_REAL(p, 4 * nc + 4) = L_eff; // used in temperature calculations
    //P_USER_REAL(p, 4 * nc + 5) = P_DIAM(p);
    //P_USER_REAL(p, 4 * nc + 6) = Dp;
    P_USER_REAL(p, 4 * nc + 5) = Nu; //used in temperature calculations
    P_USER_REAL(p, 4 * nc + 6) = T_av;

    P_USER_REAL(p, 4 * nc + 7 + N_INT + 1) = coef;
    P_USER_REAL(p, 4 * nc + 7 + N_INT + 2) = Nu_star;
    P_USER_REAL(p, 4 * nc + 7 + N_INT + 3) = D;
    P_USER_REAL(p, 4 * nc + 7 + N_INT + 4) = kgas;
    P_USER_REAL(p, 3 * nc + 0) = h;
    
    P_VAP_dhdt(p) = dh_dt;
    // FIXME under assumpmtion of mono-component droplet
    P_VAP_dmdt(p) = -dydt[1];
}

DEFINE_DPM_SCALAR_UPDATE(Diesel_droplet, cell, thread, initialize, p)
{
    int nc = TP_N_COMPONENTS(p);
    if (nc != NCOMPONENTS) {
        Message("ALARM!!! nc != NCOMPONENTS.");
    }
    real Tp = P_T(p);
    cphase_state_t *c = p->cphase;
    clock_t t;
    t = clock();
    // Message("I'm in scalar update\n");
    // FIXME Only works in steady state case
    if (initialize) {
        for (int i = 0; i < N_INT + 1; i++) { P_USER_REAL(p, 4 * nc + 7 + i) = Tp; }
        P_USER_REAL(p, 4 * nc + 6) = Tp;
        P_USER_REAL(p, 4 * nc + 5) = 2.0;
        P_USER_REAL(p, 4 * nc + 4) = p->hvap[0];
        P_USER_REAL(p, 4 * nc + 2) = 0.0; //!!!
        P_USER_REAL(p, 4 * nc + 3) = 0.0; //!!!
        P_USER_REAL(p, 4 * nc + 1) = 0.e-15;
        P_USER_REAL(p, 4 * nc + 7 + N_INT + 1) = P_DIAM(p);
        P_USER_REAL(p, 4 * nc + 7 + N_INT + 2) = DPM_DIAM_FROM_VOL(P_MASS(p) / P_RHO(p));
        // P_USER_REAL(p, 4 * nc + 7 + N_INT + 2) = 8.74856E-06;
        // P_USER_REAL(p, 4 * nc + 7 + N_INT + 4) = 0.0;
        //P_USER_REAL(p, 4 * nc + 7 + N_INT + 3) = ((real) t)/CLOCKS_PER_SEC;
        //P_USER_REAL(p, 4 * nc + 7 + N_INT + 4) = 0.0;
        // Message("Temperature initiated\n");
        for (int i = FLA_OFFSET; i < FLA_OFFSET + FLA_N_SCAL; i++) {
            P_USER_REAL(p, i) = 0;
        }
        J_DET(p) = 1.; N_P(p)   = 1.;
        J11(p)   = 1.; J22(p)   = 1.;
        // R_0(p) = 
    } else {
        // BEGIN FLA calculation 
        // Compute jacobian along trajectory.
        fla_rk4_step(p, cell, thread);
        // Compute new determinant of the jacobian:
        real div = J11(p)*J22(p) - J12(p)*J21(p);
        // Check if jacobian changed sign:
        if (signbit(J_DET(p)) != signbit(div)) {
            N_J_SIGN(p)++;
        }
        J_DET(p) = div;
        N_P(p)  = 1./fabs(div);
        // END FLA calculation 
        
        P_VAP_dhdt_scaled(p) = P_VAP_dhdt(p)*N_P(p);
        P_VAP_dmdt_scaled(p) = P_VAP_dmdt(p)*N_P(p);

        //P_USER_REAL(p, 4 * nc + 7 + N_INT + 4) = ((real)t) / CLOCKS_PER_SEC - P_USER_REAL(p, 4 * nc + 7 + N_INT + 3);
        //
        // IMPORTANT for heating and evaporation
        //
        p->state.temp = P_USER_REAL(p, 4 * nc + 6);
    }
}

DEFINE_DPM_TIMESTEP(Constant_dt, p, dt)
{
    return DPM_DT;
}

// BEGIN n-dodecane properties
DEFINE_DPM_PROPERTY(Diesel_liquid_density, c, t, p, T)
{
    return get_liquid_density(P_T(p));
}

DEFINE_DPM_PROPERTY(Diesel_liquid_specific_heat, c, t, p, T)
{
    return get_liquid_c_p(P_T(p));
}

DEFINE_DPM_PROPERTY(Diesel_latent_heat, c, t, p, T)
{
    real Tp;//temperature at the surface
    int nc = TP_N_COMPONENTS(p);
    // we check if user_reals were initialized
    if (P_USER_REAL(p, 4 * nc + 7 + N_INT) < P_T(p)) Tp = P_T(p);
    else Tp = P_USER_REAL(p, 4 * nc + 7 + N_INT);
    return get_liquid_latent_heat(Tp);
}

DEFINE_DPM_PROPERTY(Diesel_binary_diffusivity, c, t, p, T)
{
    real D, T_ref;
    cphase_state_t *carr = p->cphase;  /* continuous phase struct, caching variables of the cell */
    int nc = TP_N_COMPONENTS(p);
    // we check if user_reals were initialized
    if (P_USER_REAL(p, 4 * nc + 7 + N_INT) < P_T(p)) T_ref = (2.0*P_T(p) + carr->temp) / 3.0;
    else T_ref = (2.0*P_USER_REAL(p, 4 * nc + 7 + N_INT) + carr->temp) / 3.0;
    D = get_vapour_binary_diffusivity(carr->pressure, T_ref);
    return D;
}

DEFINE_DPM_PROPERTY(Diesel_saturation_vapour_pressure, c, t, p, T)
{
    return get_vapour_saturation_pressure(P_T(p));
}
// END n-dodecane properties

// same as before, only k_l = k_l*1000
DEFINE_DPM_HEAT_MASS(multivap_conv_diffusion_kl, p, Cp, hgas, hvap, cvap_surf, Z, dydt, dzdt)
{
    int ns;
    real Bt = 0.;
    real tot_vap_rate = 0.;
    real vap_rate;
    int nc = TP_N_COMPONENTS(p);
    Thread *t0 = P_CELL_THREAD(p);
    Material *gas_mix = THREAD_MATERIAL(DPM_THREAD(t0, p));
    Material *cond_mix = P_MATERIAL(p);
    cphase_state_t *c = p->cphase;  /* continuous phase struct, caching variables of the cell */
    real Tp;
    real mp = P_MASS(p);
    /* molecular weight in bulk gas */
    real molwt_bulk = 0.;

    //real Dp = DPM_DIAM_FROM_VOL(mp / P_RHO(p));
    real Dp = P_DIAM(p);
    real Ap = DPM_AREA(Dp);
    real Re, Pr, Nu, Sh, Sc, Nu_star, Sh_Star;
    real Ys, Y_inf, Ys_tot, rho_gas_s, xs_tot, xsM_tot;
    real BM, BT, BT_i, FBM, FBT;
    real x_surf, P_sat, Visc_l, k_l, C_pl;
    real kgas;
    real T_ref;
    int i, ret, j;
    real c_p_die;
    Material * cond_c = MIXTURE_COMPONENT(cond_mix, 0);
    real D;
    real L_eff;
    real convective_heating_rate = 0.;
    real T_av;
    real rel_vel, Pe, k_eff;
    real h0, zeta, kappa;
    real lambda[N_Lambda];
    real series[N_Lambda];
    real I_n, b_n;
    real T_eff, dif, coef, phi, dh_dt, h, factor;



    Tp = P_USER_REAL(p, 4 * nc + 7 + N_INT);
    T_ref = (c->temp + 2.0*Tp) / 3.0;
    //  c_p_die = c->sHeat;
    //  kgas = 0.02667*((c->temp + 2.0*Tp) / 900.0) - 0.02087;
    kgas = c->tCond;
    //  real molwt_vap, molwt_vap;
    Tp = P_USER_REAL(p, 4 * nc + 7 + N_INT);
    rho_gas_s = c->pressure / (287.01625988193461525183829875375*T_ref);
    Re = p->Re;
    Pr = c->sHeat * c->mu / kgas;

    /* Heat transfer coefficient */
    /* molecular weight of gas species */
    /* molwt_bulk is first the reciproke of the real molecular weight to avoid additional divisions */
    mixture_species_loop_i(gas_mix, ns)
    {
        molwt_bulk += c->yi[ns] / solver_par.molWeight[ns]; /* c->yi[ns] contains mass fraction of fluid cell */
        //P_USER_REAL(p, 3 * nc + ns) = solver_par.molWeight[ns];
    }
    molwt_bulk = MAX(molwt_bulk, DPM_SMALL);
    molwt_bulk = 1. / molwt_bulk;

    /* when not using Runge Kutta solver, increase limiting time for the next integration step */
    if (!p->in_rk) { p->limiting_time = P_DT(p)*1.01;}

    T_av = Tp;
    for (j = 1; j < N_INT; j += 2) T_av += 4.0 * P_USER_REAL(p, 4 * nc + 7 + j)*(((double)j)*Delta_R)*(((double)j)*Delta_R);
    for (j = 2; j < N_INT; j += 2) T_av += 2.0 * P_USER_REAL(p, 4 * nc + 7 + j)*(((double)j)*Delta_R)*(((double)j)*Delta_R);
    T_av = T_av*Delta_R;

    /* loop over all components of multi component particle */
    Ys = 0.e-15;
    Y_inf = 0.e-15;
    L_eff = 0.e-15;
    Ys_tot = 0.e-15;
    xs_tot = 0.e-15;
    xsM_tot = 0.e-15;
    for (ns = 0; ns < nc; ns++)
    {
        /* gas species index of vaporization */
        int gas_index = TP_COMPONENT_INDEX_I(p, ns);
        if (gas_index >= 0)
        {
            //P_sat = exp(8.1948 - 7.8099*(300.0 / Tp) - 9.0098*(300.0 / Tp)*(300.0 / Tp))*1.e5;
            //if (Tp>0.99*659.0) P_sat = P_sat*exp(15.0*(Tp / 0.99 / 659.0 - 1.0));
            P_sat = DPM_VAPOR_PRESSURE(p, cond_c, Tp);
            //real x_surf = cvap_surf[ns] * Z * UNIVERSAL_GAS_CONSTANT * Tp / c->pressure;
            x_surf = P_sat / c->pressure; //Saturation pressure for n-Dodecane from Abramzon&Sazhin 2006
            P_USER_REAL(p, ns) = x_surf;
            xs_tot += x_surf*solver_par.molWeight[ns];
            xsM_tot += x_surf;
        }
    }
    xs_tot += (1.0 - xsM_tot)*28.967; //air
    for (ns = 0; ns < nc; ns++)
    {
        /* gas species index of vaporization */
        int gas_index = TP_COMPONENT_INDEX_I(p, ns);
        if (gas_index >= 0)
        {
            //Ys = x_surf / ( x_surf + (1.-x_surf) * molwt_bulk / solver_par.molWeight[gas_index]);
            Ys = P_USER_REAL(p, ns)* solver_par.molWeight[gas_index] / xs_tot;//!!
            Y_inf += c->yi[gas_index];
            //L_eff += Ys * p->hvap[gas_index];
            L_eff += Ys * p->hvap[gas_index];
            //if (Tp>0.99*659.0) L_eff = Ys * 37.44*pow(659.0 - 653.0, 0.38)*1000.0;;
            Ys_tot += Ys;
            P_USER_REAL(p, nc + ns) = Ys;
        }
    }
    L_eff = L_eff / Ys_tot;
    Tp = P_USER_REAL(p, 4 * nc + 7 + N_INT);
    T_ref = (c->temp + 2.0*Tp) / 3.0;
    c_p_die = (0.2979 + 1.4394*(T_ref / 300.0) - 0.1351*(T_ref / 300.0)*(T_ref / 300.0))*1000.0; //vapour
    //c_p_die = c->Cp;
    //D = 0.527*pow(T_ref / 300.0, 1.583) / c->pressure;
    D = DPM_BINARY_DIFFUSIVITY(p, cond_c, Tp);
    rho_gas_s = c->pressure / (287.01625988193461525183829875375*T_ref);
    Sc = c->mu / (rho_gas_s * D);

    //  BM = (Ys_tot - Y_inf) / (1.0 - Ys_tot);
    BM = (Ys_tot) / (1.0 - Ys_tot);
    FBM = pow(1.0 + BM, 0.7)*log(1.0 + BM) / BM;
    Sh_Star = 2.0 + (pow(1.0 + Re*Sc, 1.0 / 3.0)*MAX(1.0, pow(Re, 0.077)) - 1.0) / FBM;
    Sh = log(1.0 + BM)*Sh_Star;
    //Sh = log(1.0 + BM)*(2.0 + 0.6*sqrt(Re)*pow(Sc, 1.0 / 3.0));
    tot_vap_rate = Ap * D * rho_gas_s * Sh / Dp;
    P_USER_REAL(p, 4 * nc) = Ys_tot;
    P_USER_REAL(p, 4 * nc + 1) = tot_vap_rate;


    BT = BM;
    BT_i = BT;
    dif = 1.0;
    coef = c_p_die * rho_gas_s * D / kgas * Sh_Star;
    phi = 0.e-15;
    while (dif > ACCURACY)
    {
        FBT = pow(1.0 + BT, 0.7)*log(1.0 + BT) / BT;
        Nu_star = 2.0 + (pow(1.0 + Re*Pr, 1.0 / 3.0)*MAX(1.0, pow(Re, 0.077)) - 1.0) / FBT;
        phi = coef / Nu_star;
        BT = pow(1.0 + BM, phi) - 1.0;
        dif = abs(BT - BT_i);
        BT_i = BT;
    }

    Nu = log(1.0 + BT) * Nu_star / BT;

    //dydt[0] -= L_eff * tot_vap_rate / (mp * p->Cp);
    //dzdt->energy += L_eff * tot_vap_rate;
    Visc_l = 1.e-3*exp(2.0303*(300.0 / T_av)*(300.0 / T_av) + 1.1769*(300.0 / T_av) - 2.929);
    k_l = 0.1405 - 0.00022*(T_av - 300.0);
    k_l *= 1000.0;
    C_pl = p->Cp;

    rel_vel = sqrt((c->V[0] - P_VEL(p)[0])*(c->V[0] - P_VEL(p)[0]) + (c->V[1] - P_VEL(p)[1])*(c->V[1] - P_VEL(p)[1]) + (c->V[2] - P_VEL(p)[2])*(c->V[2] - P_VEL(p)[2]));
    Pe = 12.69 / 16.0*P_RHO(p)*0.5*Dp* C_pl / k_l*rel_vel*c->mu / Visc_l*pow(Re, 1.0 / 3.0) / (1.0 + BM);
    k_eff = (1.86 + 0.86*tanh(2.225*log10(Pe / 30.0)))*k_l;
    if (abs(Pe) < 1.e-12) k_eff = k_l;

    T_eff = c->temp - tot_vap_rate*L_eff / PI / Dp / Nu / kgas;
    h0 = kgas*Nu*0.5 / k_eff - 1.0;
    zeta = (h0 + 1.0)*T_eff;
    kappa = k_eff / (C_pl*P_RHO(p)*0.25*Dp*Dp);

    for (i = 0; i < N_Lambda; i++) {lambda[i] = -1.0;};
    ret = Lambda(h0, lambda);
    for (i = 0; i < N_Lambda; i++) {series[i] = 0.e-15;};

    for (i = 0; i < N_Lambda; i++)
    {
        b_n = 0.5*(1.0 + h0 / (h0*h0 + lambda[i] * lambda[i]));
        I_n = 0.e-15;
        I_n = P_USER_REAL(p, 4 * nc + 7 + N_INT)*sin(lambda[i]);
        for (j = 1; j < N_INT; j += 2) I_n += 4.0 * P_USER_REAL(p, 4 * nc + 7 + j)*(((double)j)*Delta_R)*sin(lambda[i] * ((double)j)*Delta_R);
        for (j = 2; j < N_INT; j += 2) I_n += 2.0 * P_USER_REAL(p, 4 * nc + 7 + j)*(((double)j)*Delta_R)*sin(lambda[i] * ((double)j)*Delta_R);
        I_n = I_n*Delta_R / 3.0;
        series[i] = (I_n - sin(lambda[i]) / lambda[i] / lambda[i] * zeta)*exp(0.0 - kappa*lambda[i] * lambda[i] * P_DT(p)) / b_n;
    }

    for (j = 0; j < N_INT + 1; j++) P_USER_REAL(p, 4 * nc + 7 + j) = T_eff;
    for (i = 0; i < N_Lambda; i++)
    {
        P_USER_REAL(p, 4 * nc + 7) += series[i] * lambda[i];
        for (j = 1; j < N_INT + 1; j++) P_USER_REAL(p, 4 * nc + 7 + j) += series[i] * sin(lambda[i] * ((double)j)*Delta_R) / (((double)j)*Delta_R);
    }


    Tp = P_USER_REAL(p, 4 * nc + 7 + N_INT);
    T_ref = (c->temp + 2.0*Tp) / 3.0;
    c_p_die = (0.2979 + 1.4394*(T_ref / 300.0) - 0.1351*(T_ref / 300.0)*(T_ref / 300.0))*1000.0;
    D = 0.527*pow(T_ref / 300.0, 1.583) / c->pressure;
    rho_gas_s = c->pressure / (287.01625988193461525183829875375*T_ref);
    Sc = c->mu / (rho_gas_s * D);


    for (ns = 0; ns < nc; ns++)
    {
        /* gas species index of vaporization */
        int gas_index = TP_COMPONENT_INDEX_I(p, ns);
        if (gas_index >= 0)
        {
            vap_rate = P_USER_REAL(p, nc + ns) * tot_vap_rate / Ys_tot; //!!

            if (!p->in_rk)
                if (ABS(vap_rate)>0.)
                    p->limiting_time = MIN(p->limiting_time, dpm_par.fractional_change_factor_mass*P_MASS(p) / vap_rate*TP_COMPONENT_I(p, ns));

            P_USER_REAL(p, 2 * nc + ns) = vap_rate;
            dydt[1 + ns] -= vap_rate;

            {
                int source_index = injection_par.yi2s[gas_index];
                if (source_index >= 0)
                {
                    dzdt->species[source_index] += vap_rate;
                    //p->source.mtc[source_index] = c->rho * Ap * Sh_Star * D / Dp;
                    p->source.mtc[source_index] = c->rho * PI * Dp * Sh_Star * D;
                }
            }
        }
    }


    //p->source.htc = Nu*kgas*P_DIAM(p);
    p->source.htc = 0.e-15;
    dh_dt = Nu * kgas * Ap / Dp * (c->temp - T_av);

    h = Nu * kgas / Dp;
    convective_heating_rate = h * Ap / (mp * p->Cp);

    //dydt[0] += dh_dt / (mp * p->Cp);
    //dzdt->energy -= dh_dt;
    /* limit for higher heating rate */
    if (!p->in_rk)
    {
        if (ABS(convective_heating_rate)>DPM_SMALL)
        {
            factor = dpm_par.fractional_change_factor_heat;
            if (ABS(c->temp - Tp)>Tp)
                factor = dpm_par.fractional_change_factor_heat*Tp / (c->temp - Tp);
            p->limiting_time = MIN(p->limiting_time, factor / ABS(convective_heating_rate));
        }
    }
    P_USER_REAL(p, 4 * nc + 2) = BM;
    P_USER_REAL(p, 4 * nc + 3) = BT;
    P_USER_REAL(p, 4 * nc + 4) = L_eff; // used in temperature calculations
    //P_USER_REAL(p, 4 * nc + 5) = P_DIAM(p);
    //P_USER_REAL(p, 4 * nc + 6) = Dp;
    P_USER_REAL(p, 4 * nc + 5) = Nu; //used in temperature calculations
    P_USER_REAL(p, 4 * nc + 6) = T_av;
    p->state.temp = T_av;
    P_USER_REAL(p, 4 * nc + 7 + N_INT + 1) = P_DIAM(p);
    P_USER_REAL(p, 4 * nc + 7 + N_INT + 2) = DPM_DIAM_FROM_VOL(mp / P_RHO(p));
    //P_USER_REAL(p, 4 * nc + 7 + N_INT + 3) = k_l;
    //P_USER_REAL(p, 4 * nc + 7 + N_INT + 4) = C_pl;
    P_USER_REAL(p, 3 * nc + 0) = Pe;
    dydt[0] = 0.e-15;
}
