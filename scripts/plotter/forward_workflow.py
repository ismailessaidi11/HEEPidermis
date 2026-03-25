import numpy as np
from dataclasses import dataclass

@dataclass
class forward_input:
    G_uS: float
    i_dc_uA: float
    fs_Hz: float

@dataclass
class forward_output:
    delta_G_uS: float

    P_idc_uW: float
    P_vco_uW: float
    P_cnt_uW: float
    P_tot_uW: float

@dataclass
class forward_intermediate_variables:
    vin_mV: float
    f_osc_kHz: float
    kvco_kHz_per_mV: float

    df_osc_sampling_Hz: float
    df_osc_adev_Hz: float
    df_osc_Hz: float
    dVin_mV: float

@dataclass
class ForwardPointResult:
    input: forward_input
    intermediate: forward_intermediate_variables
    output: forward_output


def forward_compute(model, input: forward_input, variance=1, avg_window=1):

        vin_mV = model.vin_from_G(input.G_uS, input.i_dc_uA)
        f_osc_kHz = model.fosc_from_vin(vin_mV)
        kvco_kHz_per_mV = model.kvco_kHz_per_mV(vin_mV)

        df_osc_sampling_Hz = model.df_osc_sampling_Hz(fs_Hz=input.fs_Hz, avg_window=avg_window)
        df_osc_adev_Hz = variance * model.df_osc_adev_Hz(vin_mV=vin_mV, fs_Hz=input.fs_Hz)
        df_osc_Hz = max(df_osc_sampling_Hz, df_osc_adev_Hz)
        dVin_mV = model.dVin_mV(df_osc_Hz=df_osc_Hz, vin_mV=vin_mV)

        delta_G_uS = model.delta_G_uS(vin_mV=vin_mV, i_dc_uA=input.i_dc_uA, fs_Hz=input.fs_Hz,
                                    variance=variance, avg_window=avg_window)

        P_idc_uW = model.idc_power_uW(vin_mV, input.i_dc_uA)
        P_vco_uW = model.pvco_from_vin(vin_mV)
        P_cnt_uW = model.pcnt_from_vin(vin_mV)
        P_tot_uW = P_idc_uW + P_vco_uW + P_cnt_uW

        return ForwardPointResult(

            input=input,
            intermediate=forward_intermediate_variables(
                vin_mV=vin_mV,
                f_osc_kHz=f_osc_kHz,
                kvco_kHz_per_mV=kvco_kHz_per_mV,
                df_osc_sampling_Hz=df_osc_sampling_Hz,
                df_osc_adev_Hz=df_osc_adev_Hz,
                df_osc_Hz=df_osc_Hz,
                dVin_mV=dVin_mV
            ),
            output=forward_output(
                delta_G_uS=delta_G_uS,
                P_idc_uW=P_idc_uW,
                P_vco_uW=P_vco_uW,
                P_cnt_uW=P_cnt_uW,
                P_tot_uW=P_tot_uW
            )
        )