// Copyright 2025 EPFL and contributors
// Solderpad Hardware License, Version 2.1, see LICENSE.md for details.
// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
//
// File: resistor.sv
// Author: Juan Sapriza
// Date: 20/11/2025
// Description: Reads integer G codes from TXT, converts to conductance (S) and resistance (Ohm)
// Mapping: 2**16 -> 100 µS
// Output changes every 1/CHANGE_RATE_HZ seconds based on $time (ns), no #delays.

module resistor #(
    string FILE_NAME      = "../../../hw/ip/analog_subsystem/conductance.txt",
    real   CHANGE_RATE_HZ = 100.0
) (
    input  logic refresh,
    output real  r_ohm
);

    // scaling: one code LSB in Siemens
    localparam real        G_LSB    = 100e-6 / (2.0**16); // 100 µS full scale
    localparam real        R_MAX_R  = 1.0e12;
    localparam int unsigned STEP_NS = (CHANGE_RATE_HZ > 0.0)
                                      ? int'(1.0e9 / CHANGE_RATE_HZ)
                                      : 32'h7fffffff;

    int  fd;
    int  g_code;
    real g_siemens;

    real r_table[$];

    // time bookkeeping
    longint unsigned last_step_ns;

    // file read and table build
    initial begin
        fd = $fopen(FILE_NAME, "r");
        if (fd == 0) begin
            $fatal(1, "Cannot open %s", FILE_NAME);
        end

        r_table.delete();
        while (!$feof(fd)) begin
            if ($fscanf(fd, "%d", g_code) == 1) begin
                g_siemens = g_code * G_LSB;
                if (g_siemens <= 0.0)
                    r_table.push_back(R_MAX_R);
                else
                    r_table.push_back(1.0 / g_siemens);
            end
        end
        $fclose(fd);

        $display("Loaded %0d G samples into resistance table", r_table.size());
        $display("Update rate: %0d ns",STEP_NS);


        r_ohm        = (r_table.size() != 0) ? r_table[0] : R_MAX_R;
        last_step_ns = 0;
        idx          = 0;
        r_ohm        = (r_table.size() != 0) ? r_table[0] : R_MAX_R;
    end

    int unsigned idx;

    // Time-based update driven by REFRESH events, using $time in ns
    always_ff @(posedge refresh) begin
        longint unsigned now_ns   = $time;
        longint unsigned dt_ns    = now_ns - last_step_ns;
        longint unsigned steps;

        // verilator lint_off WIDTH
        if (dt_ns >= STEP_NS) begin
            steps = dt_ns / STEP_NS;
            idx   <= (idx + steps) % r_table.size();
            r_ohm <= r_table[(idx + steps) % r_table.size()];
            last_step_ns <= last_step_ns + steps * STEP_NS;
        end
        // verilator lint_on WIDTH
    end

endmodule
