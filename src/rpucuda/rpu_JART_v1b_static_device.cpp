/**
 * (C) Copyright 2020, 2021, 2022 IBM. All Rights Reserved.
 *
 * This code is licensed under the Apache License, Version 2.0. You may
 * obtain a copy of this license in the LICENSE.txt file in the root directory
 * of this source tree or at http://www.apache.org/licenses/LICENSE-2.0.
 *
 * Any modifications or derivative works of this code must retain this
 * copyright notice, and modified files need to carry a notice indicating
 * that they have been altered from the originals.
 */

#include "rpu_JART_v1b_static_device.h"

namespace RPU {

/********************************************************************************
 * JART v1b Static RPU Device
 *********************************************************************************/

template <typename T>
void JARTv1bStaticRPUDevice<T>::populate(
    const JARTv1bStaticRPUDeviceMetaParameter<T> &p, RealWorldRNG<T> *rng) {

    // fix weight granularity
    int *dw_min_ptr;
    dw_min_ptr = (int*)(&p.dw_min);
    *dw_min_ptr = fix_weight_granularity(p.w_min, p.w_max, p);

  PulsedRPUDevice<T>::populate(p, rng); // will clone par
  auto &par = getPar();

  // We use hidden weight w as Ndisk, and the write noised weight w_apprent as true w mapped from conductance.
  // So we need to force usesPersistentWeight() = True 
  // this->write_noise_std = 1;

  for (int i = 0; i < this->d_size_; ++i) {

    for (int j = 0; j < this->x_size_; ++j) {
      device_specific_Ndiscmax[i][j] = par.Ndiscmax + par.Ndiscmax_dtod * rng->sampleGauss();
      device_specific_Ndiscmin[i][j] = par.Ndiscmin + par.Ndiscmin_dtod * rng->sampleGauss();
      device_specific_ldet[i][j] = par.ldet + par.ldet_dtod * rng->sampleGauss();
      T device_specific_rdet = par.rdet + par.rdet_dtod * rng->sampleGauss();
      device_specific_A[i][j] = (T) M_PI*pow(device_specific_rdet,2);
    }
  }
}

template <typename T> void JARTv1bStaticRPUDevice<T>::printDP(int x_count, int d_count) const {

  if (x_count < 0 || x_count > this->x_size_) {
    x_count = this->x_size_;
  }

  if (d_count < 0 || d_count > this->d_size_) {
    d_count = this->d_size_;
  }
  bool persist_if = getPar().usesPersistentWeight();

  for (int i = 0; i < d_count; ++i) {
    for (int j = 0; j < x_count; ++j) {
      std::cout.precision(5);
      std::cout << i << "," << j << ": ";
      std::cout << device_specific_Ndiscmax[i][j] << ",";
      std::cout << device_specific_Ndiscmin[i][j] << ",";
      std::cout << device_specific_ldet[i][j] << ",";
      std::cout << device_specific_A[i][j] << ">]";
      std::cout.precision(10);
      std::cout << this->w_decay_scale_[i][j] << ", ";
      std::cout.precision(6);
      std::cout << this->w_diffusion_rate_[i][j] << ", ";
      std::cout << this->w_reset_bias_[i][j];
      if (persist_if) {
        std::cout << ", " << this->w_persistent_[i][j];
      }
      std::cout << "]";
    }
    std::cout << std::endl;
  }
}


template <typename T>
struct Voltages_holder
{
  T V_series;
  T V_disk;
  T V_plug;
  T V_Schottky;
};

template <typename T>
inline T calculate_current_negative(
    T &Ndisc,
    const T &applied_voltage,
    const T &alpha0,
    const T &alpha1,
    const T &alpha2,
    const T &alpha3,
    const T &beta0,
    const T &beta1,
    const T &c0,
    const T &c1,
    const T &c2,
    const T &c3,
    const T &d0,
    const T &d1,
    const T &d2,
    const T &d3,
    const T &f0,
    const T &f1,
    const T &f2,
    const T &f3) {
  return -(((alpha1+alpha0)/(1+exp(-(applied_voltage+alpha2)/alpha3)))-alpha0)-((beta1*(1-exp(-applied_voltage)))
  -beta0*applied_voltage)/(pow(((1+pow(((c2*exp(-applied_voltage/c3)+c1*applied_voltage-c0)/(Ndisc/1e26)),(d2*exp(-applied_voltage/d3)+d1*applied_voltage-d0)))),(f0+((f1-f0)/(1+pow((-applied_voltage/f2),f3))))));
}

template <typename T>
inline T calculate_current_positive(
    T &Ndisc,
    const T &applied_voltage,
    const T &g0,
    const T &g1,
    const T &h0,
    const T &h1,
    const T &h2,
    const T &h3,
    const T &j_0,
    const T &k0, 
    const T &Ndiscmin) {
  return (-g0*(exp(-g1*applied_voltage)-1))/(pow((1+(h0+h1*applied_voltage+h2*exp(-h3*applied_voltage))*pow((Ndisc/Ndiscmin),(-j_0))),(1/k0)));
}

template <typename T>
inline T invert_positive_current(
    T &I_mem,
    const T &read_voltage,
    const T &g0,
    const T &g1,
    const T &h0,
    const T &h1,
    const T &h2,
    const T &h3,
    const T &j_0,
    const T &k0, 
    const T &Ndiscmin) {
  if (I_mem>0){
  return pow(((pow(((-g0*(exp(-g1*read_voltage)-1))/I_mem), k0)-1)/(h0+h1*read_voltage+h2*exp(-h3*read_voltage))),(1/-j_0))*Ndiscmin;
  }
  else{
    return 0;
  }
}

template <typename T>
inline T calculate_current(
    T &Ndisc,
    const T &applied_voltage,
    const T &alpha0,
    const T &alpha1,
    const T &alpha2,
    const T &alpha3,
    const T &beta0,
    const T &beta1,
    const T &c0,
    const T &c1,
    const T &c2,
    const T &c3,
    const T &d0,
    const T &d1,
    const T &d2,
    const T &d3,
    const T &f0,
    const T &f1,
    const T &f2,
    const T &f3,
    const T &g0,
    const T &g1,
    const T &h0,
    const T &h1,
    const T &h2,
    const T &h3,
    const T &j_0,
    const T &k0, 
    const T &Ndiscmin) {
  if (applied_voltage < 0) {
    return calculate_current_negative(Ndisc, applied_voltage, alpha0, alpha1, alpha2, alpha3, beta0, beta1, c0, c1, c2, c3, d0, d1, d2, d3, f0, f1, f2, f3);
  } else {
    return calculate_current_positive(Ndisc, applied_voltage, g0, g1, h0, h1, h2, h3, j_0, k0, Ndiscmin);
  }
}

template <typename T>
inline T calculate_T(
    const T &applied_voltage,
    T &I_mem,
    const T &T0,
    const T &Rth0,
    const T &Rtheff_scaling,
    Voltages_holder<T> &Voltages) {
  if (applied_voltage > 0) {
    return T0 + I_mem*(Voltages.V_disk+Voltages.V_plug+Voltages.V_Schottky)*Rth0*Rtheff_scaling;
  } else {
    return T0 + I_mem*(Voltages.V_disk+Voltages.V_plug+Voltages.V_Schottky)*Rth0;
  }
}

template <typename T>
inline Voltages_holder<T> calculate_voltages(
    const T &applied_voltage,
    T &I_mem,
    const T &R0,
    const T &alphaline,
    const T &Rthline,
    const T &RseriesTiOx,
    const T &lcell,
    T &ldet,
    const int &zvo,
    const T &e,
    T &A,
    const T &Nplug,
    T &Ndisc,
    const T &un) {
  Voltages_holder<T> Voltages;
  // V_series
  Voltages.V_series = I_mem*(RseriesTiOx + (R0*(1+alphaline*R0*pow(I_mem,2)*Rthline)));
  // V_disk
  Voltages.V_disk =  I_mem*(ldet/(zvo*e*A*Ndisc*un));
  // V_plug
  Voltages.V_plug =  I_mem*((lcell-ldet)/(zvo*e*A*Nplug*un));
  // V_Schottky
  Voltages.V_Schottky =  applied_voltage-Voltages.V_series-Voltages.V_disk-Voltages.V_plug;
  return Voltages;
}

template <typename T>
inline T calculate_F1(
    const T &applied_voltage,
    T &Ndisc,
    T &Ndiscmin,
    T &Ndiscmax) {
  if (applied_voltage > 0) {
    return 1-pow((Ndiscmin/Ndisc),10);
  } else {
    return 1-pow((Ndisc/Ndiscmax),10);
  }
}

template <typename T>
inline T calculate_Eion(
    const T &applied_voltage,
    Voltages_holder<T> &Voltages,
    const T &lcell,
    T &ldet) {
  if (applied_voltage > 0) {
    return Voltages.V_disk/ldet;
  } else {
    return (Voltages.V_Schottky + Voltages.V_plug + Voltages.V_disk)/lcell;
  }
}

template <typename T>
inline T calculate_dNdt(
    const T &applied_voltage,
    T &I_mem,
    T &Ndisc,
    const T &e,
    const T &kb,	
    const T &Arichardson,
    const T &mdiel,
    const T &h,
    const int &zvo,
    const T &eps_0,
    const T &T0,
    const T &eps,
    const T &epsphib,
    const T &phiBn0,
    const T &phin,
    const T &un,
    T &Ndiscmax,
    T &Ndiscmin,
    const T &Nplug,
    const T &a,
    const T &ny0,
    const T &dWa,
    const T &Rth0,
    const T &lcell,
    T &ldet,
    const T &Rtheff_scaling,
    const T &RseriesTiOx,
    const T &R0,
    const T &Rthline,
    const T &alphaline,
    T &A) {

  T c_v0 = (Nplug+Ndisc)/2;

  T F1 = calculate_F1(applied_voltage, Ndisc, Ndiscmin, Ndiscmax);

  Voltages_holder<T> Voltages = calculate_voltages(applied_voltage, I_mem, R0, alphaline, Rthline, RseriesTiOx, lcell, ldet, zvo, e, A, Nplug, Ndisc, un);

  T Eion = calculate_Eion(applied_voltage, Voltages, lcell, ldet);

  T gamma = zvo*a*Eion/(dWa*M_PI);

  T Treal = calculate_T(applied_voltage, I_mem, T0, Rth0, Rtheff_scaling, Voltages);
  
  // dWamin
  T dWa_f = dWa*(sqrt(1-pow(gamma,2))-(gamma*M_PI)/2+gamma*asin(gamma));
  // dWamax
  T dWa_r = dWa*(sqrt(1-pow(gamma,2))+(gamma*M_PI)/2+gamma*asin(gamma));
  T denominator = kb*Treal/e;
  T dNdt = -(c_v0*a*ny0*F1*(exp(-dWa_f/denominator)-exp(-dWa_r/denominator)))/ldet;
  return dNdt;
}

template <typename T>
inline void step(
    const T &applied_voltage,
    const T &time_step,
    T &Ndisc,
    const T &alpha0,
    const T &alpha1,
    const T &alpha2,
    const T &alpha3,
    const T &beta0,
    const T &beta1,
    const T &c0,
    const T &c1,
    const T &c2,
    const T &c3,
    const T &d0,
    const T &d1,
    const T &d2,
    const T &d3,
    const T &f0,
    const T &f1,
    const T &f2,
    const T &f3,
    const T &g0,
    const T &g1,
    const T &h0,
    const T &h1,
    const T &h2,
    const T &h3,
    const T &j_0,
    const T &k0, 
    const T &e,
    const T &kb,	
    const T &Arichardson,
    const T &mdiel,
    const T &h,
    const int &zvo,
    const T &eps_0,
    const T &T0,
    const T &eps,
    const T &epsphib,
    const T &phiBn0,
    const T &phin,
    const T &un,
    const T &Original_Ndiscmin,
    T &Ndiscmax,
    T &Ndiscmin,
    const T &Nplug,
    const T &a,
    const T &ny0,
    const T &dWa,
    const T &Rth0,
    const T &lcell,
    T &ldet,
    const T &Rtheff_scaling,
    const T &RseriesTiOx,
    const T &R0,
    const T &Rthline,
    const T &alphaline,
    T &A,
    const T &Ndisc_min_bound,
    const T &Ndisc_max_bound) {
  T I_mem = calculate_current(Ndisc, applied_voltage, alpha0, alpha1, alpha2, alpha3, beta0, beta1, c0, c1, c2, c3, d0, d1, d2, d3, f0, f1, f2, f3, g0, g1, h0, h1, h2, h3, j_0, k0, Original_Ndiscmin);
  T dNdt = calculate_dNdt(applied_voltage, I_mem, Ndisc, e, kb, Arichardson, mdiel, h, zvo, eps_0, T0, eps, epsphib, phiBn0, phin, un, Ndiscmax, Ndiscmin, Nplug, a, ny0, dWa, Rth0, lcell, ldet, Rtheff_scaling, RseriesTiOx, R0, Rthline, alphaline, A);
  Ndisc = Ndisc + dNdt*time_step;
  if (Ndisc>Ndisc_max_bound){
    Ndisc = Ndisc_max_bound;
  }
  else if (Ndisc<Ndisc_min_bound){
      Ndisc = Ndisc_min_bound;
  }
}

template <typename T>
inline T map_Ndisc_to_weight(
    const T &read_voltage,
    T &Ndisc,
    const T &conductance_min,
    const T &conductance_max,
    const T &weight_min_bound,
    const T &weight_max_bound,
    const T &g0,
    const T &g1,
    const T &h0,
    const T &h1,
    const T &h2,
    const T &h3,
    const T &j_0,
    const T &k0,
    const T &Original_Ndiscmin) {
  T conductance = calculate_current_positive(Ndisc, read_voltage, g0, g1, h0, h1, h2, h3, j_0, k0, Original_Ndiscmin);
  T weight = ((conductance-conductance_min)/(conductance_max-conductance_min))*(weight_max_bound-weight_min_bound)+weight_min_bound;
  return weight;
}

template <typename T>
inline void update_once(
    const T &read_voltage,
    const T &pulse_voltage_SET,
    const T &pulse_voltage_RESET,
    const T &pulse_length,
    const T &base_time_step,
    const T &alpha0,
    const T &alpha1,
    const T &alpha2,
    const T &alpha3,
    const T &beta0,
    const T &beta1,
    const T &c0,
    const T &c1,
    const T &c2,
    const T &c3,
    const T &d0,
    const T &d1,
    const T &d2,
    const T &d3,
    const T &f0,
    const T &f1,
    const T &f2,
    const T &f3,
    const T &g0,
    const T &g1,
    const T &h0,
    const T &h1,
    const T &h2,
    const T &h3,
    const T &j_0,
    const T &k0, 
    const T &e,
    const T &kb,	
    const T &Arichardson,
    const T &mdiel,
    const T &h,
    const int &zvo,
    const T &eps_0,
    const T &T0,
    const T &eps,
    const T &epsphib,
    const T &phiBn0,
    const T &phin,
    const T &un,
    const T &Original_Ndiscmin,
    T &Ndiscmax,
    T &Ndiscmin,
    const T &Nplug,
    const T &a,
    const T &ny0,
    const T &dWa,
    const T &Rth0,
    const T &lcell,
    T &ldet,
    const T &Rtheff_scaling,
    const T &RseriesTiOx,
    const T &R0,
    const T &Rthline,
    const T &alphaline,
    T &A,
    T &w,
    T &w_apparent,
    int &sign,
    const T &conductance_min,
    const T &conductance_max,
    const T &weight_min_bound,
    const T &weight_max_bound,
    const T &Ndisc_min_bound,
    const T &Ndisc_max_bound) {
  int pulse_counter = (int) pulse_length/base_time_step;
  if (sign > 0) {
    for (int i = 0; i < pulse_counter; i++) {
      step(pulse_voltage_SET, base_time_step, w, alpha0, alpha1, alpha2, alpha3, beta0, beta1, c0, c1, c2, c3, d0, d1, d2, d3, f0, f1, f2, f3, g0, g1, h0, h1, h2, h3, j_0, k0, e, kb, Arichardson, mdiel, h, zvo, eps_0, T0, eps, epsphib, phiBn0, phin, un, Original_Ndiscmin,  Ndiscmax, Ndiscmin, Nplug, a, ny0, dWa, Rth0, lcell, ldet, Rtheff_scaling, RseriesTiOx, R0, Rthline, alphaline, A, Ndisc_min_bound, Ndisc_max_bound);
    }
  }else{
    for (int i = 0; i < pulse_counter; i++) {
      step(pulse_voltage_RESET, base_time_step, w, alpha0, alpha1, alpha2, alpha3, beta0, beta1, c0, c1, c2, c3, d0, d1, d2, d3, f0, f1, f2, f3, g0, g1, h0, h1, h2, h3, j_0, k0, e, kb, Arichardson, mdiel, h, zvo, eps_0, T0, eps, epsphib, phiBn0, phin, un, Original_Ndiscmin,  Ndiscmax, Ndiscmin, Nplug, a, ny0, dWa, Rth0, lcell, ldet, Rtheff_scaling, RseriesTiOx, R0, Rthline, alphaline, A, Ndisc_min_bound, Ndisc_max_bound);
    }
  }

  w_apparent = map_Ndisc_to_weight(read_voltage, w, conductance_min, conductance_max, weight_min_bound, weight_max_bound, g0, g1, h0, h1, h2, h3, j_0, k0, Original_Ndiscmin);
}

template <typename T>
inline T fix_weight_granularity(
    const T &w_min,
    const T &w_max,
    const JARTv1bStaticRPUDeviceMetaParameter<T> &par) {
    T Ndiscmax_granularity=par.Ndiscmax;
    T Ndiscmin_granularity=par.Ndiscmin;
    T ldet_granularity=par.ldet;
    T A_granularity=M_PI*pow(par.rdet,2);

    T Ndisc_SET = par.Ndisc_min_bound;
    for (int i = 0; i < (int) par.pulse_length/par.base_time_step; i++) {
        step(par.pulse_voltage_SET, par.base_time_step, Ndisc_SET, par.alpha0, par.alpha1, par.alpha2, par.alpha3, par.beta0, par.beta1, par.c0, par.c1, par.c2, par.c3, par.d0, par.d1, par.d2, par.d3, par.f0, par.f1, par.f2, par.f3, par.g0, par.g1, par.h0, par.h1, par.h2, par.h3, par.j_0, par.k0, par.e, par.kb, par.Arichardson, par.mdiel, par.h, par.zvo, par.eps_0, par.T0, par.eps, par.epsphib, par.phiBn0, par.phin, par.un, par.Ndiscmin, Ndiscmax_granularity, Ndiscmin_granularity, par.Nplug, par.a, par.ny0, par.dWa, par.Rth0, par.lcell, ldet_granularity, par.Rtheff_scaling, par.RseriesTiOx, par.R0, par.Rthline, par.alphaline, A_granularity, par.Ndisc_min_bound, par.Ndisc_max_bound);
      }
    T weight_SET_1_step_from_min_bound = map_Ndisc_to_weight(par.read_voltage, Ndisc_SET, par.conductance_min, par.conductance_max, w_min, w_max, par.g0, par.g1, par.h0, par.h1, par.h2, par.h3, par.j_0, par.k0, par.Ndiscmin);
    
    T Ndisc_RESET = par.Ndisc_max_bound;
    for (int i = 0; i < (int) par.pulse_length/par.base_time_step; i++) {
        step(par.pulse_voltage_RESET, par.base_time_step, Ndisc_RESET, par.alpha0, par.alpha1, par.alpha2, par.alpha3, par.beta0, par.beta1, par.c0, par.c1, par.c2, par.c3, par.d0, par.d1, par.d2, par.d3, par.f0, par.f1, par.f2, par.f3, par.g0, par.g1, par.h0, par.h1, par.h2, par.h3, par.j_0, par.k0, par.e, par.kb, par.Arichardson, par.mdiel, par.h, par.zvo, par.eps_0, par.T0, par.eps, par.epsphib, par.phiBn0, par.phin, par.un, par.Ndiscmin, Ndiscmax_granularity, Ndiscmin_granularity, par.Nplug, par.a, par.ny0, par.dWa, par.Rth0, par.lcell, ldet_granularity, par.Rtheff_scaling, par.RseriesTiOx, par.R0, par.Rthline, par.alphaline, A_granularity, par.Ndisc_min_bound, par.Ndisc_max_bound);
      }
    T weight_RESET_1_step_from_max_bound = map_Ndisc_to_weight(par.read_voltage, Ndisc_RESET, par.conductance_min, par.conductance_max, w_min, w_max, par.g0, par.g1, par.h0, par.h1, par.h2, par.h3, par.j_0, par.k0, par.Ndiscmin);
    
    T dw_min = MIN(weight_SET_1_step_from_min_bound - w_min, w_max - weight_RESET_1_step_from_max_bound);
    return dw_min;
}

template <typename T>
void JARTv1bStaticRPUDevice<T>::doSparseUpdate(
    T **weights, int i, const int *x_signed_indices, int x_count, int d_sign, RNG<T> *rng) {

  const auto &par = getPar();

  T *w = par.usesPersistentWeight() ? this->w_persistent_[i] : weights[i];
  T *w_apparent = weights[i];
  T *Ndiscmax = device_specific_Ndiscmax[i];
  T *Ndiscmin = device_specific_Ndiscmin[i];
  T *ldet = device_specific_ldet[i];
  T *A = device_specific_A[i];

  PULSED_UPDATE_W_LOOP(update_once(par.read_voltage, par.pulse_voltage_SET, par.pulse_voltage_RESET, par.pulse_length, par.base_time_step,
                                   par.alpha0, par.alpha1, par.alpha2, par.alpha3, par.beta0, par.beta1,
                                   par.c0, par.c1, par.c2, par.c3, par.d0, par.d1, par.d2, par.d3,
                                   par.f0, par.f1, par.f2, par.f3, par.g0, par.g1, par.h0, par.h1, par.h2, par.h3, par.j_0, par.k0,
                                   par.e, par.kb, par.Arichardson, par.mdiel, par.h, par.zvo, par.eps_0,
                                   par.T0, par.eps, par.epsphib, par.phiBn0, par.phin, par.un, par.Ndiscmin,
                                   Ndiscmax[j], Ndiscmin[j],
                                   par.Nplug, par.a, par.ny0, par.dWa, par.Rth0, par.lcell,
                                   ldet[j],
                                   par.Rtheff_scaling, par.RseriesTiOx, par.R0, par.Rthline, par.alphaline,
                                   A[j], w[j], w_apparent[j], sign,
                                   par.conductance_min, par.conductance_max,
                                   par.w_min, par.w_max,
                                   par.Ndisc_min_bound, par.Ndisc_max_bound););
}

template <typename T>
void JARTv1bStaticRPUDevice<T>::doDenseUpdate(T **weights, int *coincidences, RNG<T> *rng) {

  const auto &par = getPar();

  T *w = par.usesPersistentWeight() ? this->w_persistent_[0] : weights[0];
  T *w_apparent = weights[0];
  T *Ndiscmax = device_specific_Ndiscmax[0];
  T *Ndiscmin = device_specific_Ndiscmin[0];
  T *ldet = device_specific_ldet[0];
  T *A = device_specific_A[0];

  PULSED_UPDATE_W_LOOP_DENSE(update_once(par.read_voltage, par.pulse_voltage_SET, par.pulse_voltage_RESET, par.pulse_length, par.base_time_step,
                                   par.alpha0, par.alpha1, par.alpha2, par.alpha3, par.beta0, par.beta1,
                                   par.c0, par.c1, par.c2, par.c3, par.d0, par.d1, par.d2, par.d3,
                                   par.f0, par.f1, par.f2, par.f3, par.g0, par.g1, par.h0, par.h1, par.h2, par.h3, par.j_0, par.k0,
                                   par.e, par.kb, par.Arichardson, par.mdiel, par.h, par.zvo, par.eps_0,
                                   par.T0, par.eps, par.epsphib, par.phiBn0, par.phin, par.un, par.Ndiscmin,
                                   Ndiscmax[j], Ndiscmin[j],
                                   par.Nplug, par.a, par.ny0, par.dWa, par.Rth0, par.lcell,
                                   ldet[j],
                                   par.Rtheff_scaling, par.RseriesTiOx, par.R0, par.Rthline, par.alphaline,
                                   A[j], w[j], w_apparent[j], sign,
                                   par.conductance_min, par.conductance_max,
                                   par.w_min, par.w_max,
                                   par.Ndisc_min_bound, par.Ndisc_max_bound););
}



/********************************************************************************/
/* compute functions  */

template <typename T> void JARTv1bStaticRPUDevice<T>::decayWeights(T **weights, bool bias_no_decay) {

  // This device does not have decay
}

template <typename T>
void JARTv1bStaticRPUDevice<T>::decayWeights(T **weights, T alpha, bool bias_no_decay) {

  // This device does not have decay
}

template <typename T>
void JARTv1bStaticRPUDevice<T>::driftWeights(T **weights, T time_since_last_call, RNG<T> &rng) {

  // This device does not have drift
}

template <typename T> void JARTv1bStaticRPUDevice<T>::diffuseWeights(T **weights, RNG<T> &rng) {

  // T *w = getPar().usesPersistentWeight() ? w_persistent_[0] : weights[0];
  T *w = weights[0];
  T *diffusion_rate = &(this->w_diffusion_rate_[0][0]);
  const auto &par = getPar();

  PRAGMA_SIMD
  for (int i = 0; i < this->size_; ++i) {
    w[i] += diffusion_rate[i] * rng.sampleGauss();
    w[i] = MIN(w[i], par.w_max);
    w[i] = MAX(w[i], par.w_min);
  }
  if (getPar().usesPersistentWeight()) {
    applyUpdateWriteNoise(weights);
  }
}

template <typename T> void JARTv1bStaticRPUDevice<T>::clipWeights(T **weights, T clip) {
  // apply hard bounds
  // T *w = getPar().usesPersistentWeight() ? w_persistent_[0] : weights[0];
  T *w = weights[0];
  const auto &par = getPar();
  if (clip < 0.0) { // only apply bounds
    PRAGMA_SIMD
    for (int i = 0; i < this->size_; ++i) {
      w[i] = MIN(w[i], par.w_max);
      w[i] = MAX(w[i], par.w_min);
    }
  } else {
    PRAGMA_SIMD
    for (int i = 0; i < this->size_; ++i) {
      w[i] = MIN(w[i], MIN(par.w_max, clip));
      w[i] = MAX(w[i], MAX(par.w_min, -clip));
    }
  }
  if (getPar().usesPersistentWeight()) {
    applyUpdateWriteNoise(weights);
  }
}

template <typename T>
void JARTv1bStaticRPUDevice<T>::resetCols(
    T **weights, int start_col, int n_col, T reset_prob, RealWorldRNG<T> &rng) {

  const auto &par = getPar();
  if (getPar().usesPersistentWeight()) {
    T reset_std = getPar().reset_std;
    for (int j = 0; j < this->x_size_; ++j) {
      if ((start_col + n_col <= this->x_size_ && j >= start_col && j < start_col + n_col) ||
          (start_col + n_col > this->x_size_ &&
          ((j >= start_col) || (j < n_col - (this->x_size_ - start_col))))) {
        PRAGMA_SIMD
        for (int i = 0; i < this->d_size_; ++i) {
          if (reset_prob == 1 || rng.sampleUniform() < reset_prob) {
            weights[i][j] =
                this->w_reset_bias_[i][j] + (reset_std > 0 ? reset_std * rng.sampleGauss() : (T)0.0);
            weights[i][j] = MIN(weights[i][j], par.w_max);
            weights[i][j] = MAX(weights[i][j], par.w_min);
            T current = (((weights[i][j]-par.w_min)/(par.w_max-par.w_min))*(par.conductance_max-par.conductance_min)+par.conductance_min)*par.read_voltage;
            this->w_persistent_[i][j] = invert_positive_current(current,par.read_voltage, par.g0, par.g1, par.h0, par.h1, par.h2, par.h3, par.j_0, par.k0, par.Ndiscmin);
          }
        }
      }
    }
  }

  T reset_std = getPar().reset_std;
  for (int j = 0; j < this->x_size_; ++j) {
    if ((start_col + n_col <= this->x_size_ && j >= start_col && j < start_col + n_col) ||
        (start_col + n_col > this->x_size_ &&
         ((j >= start_col) || (j < n_col - (this->x_size_ - start_col))))) {
      PRAGMA_SIMD
      for (int i = 0; i < this->d_size_; ++i) {
        if (reset_prob == 1 || rng.sampleUniform() < reset_prob) {
          weights[i][j] =
              this->w_reset_bias_[i][j] + (reset_std > 0 ? reset_std * rng.sampleGauss() : (T)0.0);
          weights[i][j] = MIN(weights[i][j], par.w_max);
          weights[i][j] = MAX(weights[i][j], par.w_min);
        }
      }
    }
  }
}

template <typename T>
void JARTv1bStaticRPUDevice<T>::resetAtIndices(
    T **weights, std::vector<int> x_major_indices, RealWorldRNG<T> &rng) {

  const auto &par = getPar();
  if (getPar().usesPersistentWeight()) {
    T reset_std = getPar().reset_std;

    for (const auto &index : x_major_indices) {
      int i = index / this->x_size_;
      int j = index % this->x_size_;

      weights[i][j] = this->w_reset_bias_[i][j] + (reset_std > 0 ? reset_std * rng.sampleGauss() : (T)0.0);
      weights[i][j] = MIN(weights[i][j], par.w_max);
      weights[i][j] = MAX(weights[i][j], par.w_min);
      T current = (((weights[i][j]-par.w_min)/(par.w_max-par.w_min))*(par.conductance_max-par.conductance_min)+par.conductance_min)*par.read_voltage;
      this->w_persistent_[i][j] = invert_positive_current(current,par.read_voltage, par.g0, par.g1, par.h0, par.h1, par.h2, par.h3, par.j_0, par.k0, par.Ndiscmin);

    }
  }

  T reset_std = getPar().reset_std;

  for (const auto &index : x_major_indices) {
    int i = index / this->x_size_;
    int j = index % this->x_size_;

    weights[i][j] = this->w_reset_bias_[i][j] + (reset_std > 0 ? reset_std * rng.sampleGauss() : (T)0.0);
    weights[i][j] = MIN(weights[i][j], par.w_max);
    weights[i][j] = MAX(weights[i][j], par.w_min);
  }
}

template <typename T> bool JARTv1bStaticRPUDevice<T>::onSetWeights(T **weights) {

  const auto &par = getPar();

  // apply hard bounds to given weights
  T *w = weights[0];
  PRAGMA_SIMD
  for (int i = 0; i < this->size_; ++i) {
    w[i] = MIN(w[i], par.w_max);
    w[i] = MAX(w[i], par.w_min);
  }

  if (getPar().usesPersistentWeight()) {
    PRAGMA_SIMD
    for (int i = 0; i < this->size_; i++) {
      T current = (((w[i]-par.w_min)/(par.w_max-par.w_min))*(par.conductance_max-par.conductance_min)+par.conductance_min)*par.read_voltage;
      this->w_persistent_[0][i] = invert_positive_current(current,par.read_voltage, par.g0, par.g1, par.h0, par.h1, par.h2, par.h3, par.j_0, par.k0, par.Ndiscmin);
    }
    // applyUpdateWriteNoise(weights);
    return true; // modified device thus true
  } else {
    return false; // whether device was changed
  }
}

// Hijacked to perform weight to Ndisk mapping
template <typename T> void JARTv1bStaticRPUDevice<T>::applyUpdateWriteNoise(T **weights) {

  const auto &par = getPar();
  
  for (int i = 0; i < this->size_; i++) {
      T current = (((weights[0][i]-par.w_min)/(par.w_max-par.w_min))*(par.conductance_max-par.conductance_min)+par.conductance_min)*par.read_voltage;
      this->w_persistent_[0][i] = invert_positive_current(current,par.read_voltage, par.g0, par.g1, par.h0, par.h1, par.h2, par.h3, par.j_0, par.k0, par.Ndiscmin);
  }
}

template class JARTv1bStaticRPUDevice<float>;
#ifdef RPU_USE_DOUBLE
template class JARTv1bStaticRPUDevice<double>;
#endif

} // namespace RPU
