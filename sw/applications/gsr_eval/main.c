#include <stdio.h>

#include "input_signal.h"   // defines signal_length and signal[] (values in nS)
#include "descompv5.h"

/* PRINTF: works for chip sim, FPGA, and native host compile */
#define PRINTF_IN_SIM  1
#define PRINTF_IN_FPGA 1

#if TARGET_SIM && PRINTF_IN_SIM
    #define PRINTF(fmt, ...) printf(fmt, ##__VA_ARGS__)
#elif PRINTF_IN_FPGA && !TARGET_SIM
    #define PRINTF(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
    #define PRINTF(...)
#endif

extern int EDA_U_diag0[];
extern int EDA_U_diag1[];
extern int EDA_U_diag2[];
extern int EDA_L_mult_1[];
extern int EDA_L_mult_2[];

static int tonic[signal_length];
static int phasic[signal_length];

int main(void)
{
    EDA_build_A_and_factorize();

    solve_v5(signal_length, signal, tonic, phasic,
             EDA_U_diag0, EDA_U_diag1, EDA_U_diag2,
             EDA_L_mult_1, EDA_L_mult_2);

    /* Metadata header so compare.py can parse parameters */
    PRINTF("# signal_length=%d line_start=500000 line_end=600000 units=nS\n",
           signal_length);
    PRINTF("idx,signal_nS,tonic,phasic\n");
    for (int i = 0; i < signal_length; i++)
        PRINTF("%d,%d,%d,%d\n", i, signal[i], tonic[i], phasic[i]);

    return 0;
}
