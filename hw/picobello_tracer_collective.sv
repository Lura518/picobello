// Copyright 2025 ETH Zurich and University of Bologna.
// Solderpad Hardware License, Version 0.51, see LICENSE for details.
// SPDX-License-Identifier: SHL-0.51
//
// Author: Raphael Roth <raroth@student.ethz.ch>

`include "cheshire/typedef.svh"
`include "floo_noc/typedef.svh"
`include "axi/assign.svh"

module picobello_floo_logger
    import cheshire_pkg::*;
    import floo_pkg::*;
    import floo_picobello_noc_pkg::*;
    import picobello_pkg::*;
#(
    // Log all collective operation (reduction + multicast) which enters the router
    parameter bit LOG_INPUT_COLL_OPERATION = 1'b0,
    // Log all collective operation (reduction + multicast) which exit the router
    parameter bit LOG_OUTPUT_COLL_OPERATION = 1'b0,
    // Log all collective operation on the narrow request
    parameter bit LOG_NARROW_REQUEST        = 1'b0,
    // Log all collective operation on the narrow request
    parameter bit LOG_NARROW_RESPOND        = 1'b0,
    // Log all collective operation on the narrow request
    parameter bit LOG_WIDE_REQUEST          = 1'b0,
    // Log the local port
    parameter bit LOG_LOCAL_PORT            = 1'b0,
    // Support virtual channel for wide link
    parameter bit LOG_VIRTUAL_WIDE          = 1'b0,
    // Support virtual channel for narrow link
    parameter bit LOG_VIRTUAL_NARROW        = 1'b0,
    // Name of the tile
    parameter string tile_type              = "Test"
) (
    input logic clk_i,
    // Chimney ports
    input id_t id_i,
    // Router ports
    input floo_req_t [Eject:North] floo_req_out,
    input floo_req_t [Eject:North] floo_req_in,
    input floo_rsp_t [Eject:North] floo_rsp_out,
    input floo_rsp_t [Eject:North] floo_rsp_in,
    input floo_wide_t [Eject:North] floo_wide_out,
    input floo_wide_t [Eject:North] floo_wide_in
);
    // Disable the synth for these modules!
    `ifndef SYNTHESIS

    // Type of collective operation
    string op_codes_parallel [16] = {"SelectAW", "CollectB", "LSBAnd", "Inv", "Inv", "Inv", "Inv", "Inv", "Inv", "Inv", "Inv", "Inv", "Inv", "Inv", "Inv", "Inv"};
    string op_codes_offload [16] = {"R_Select", "Inv", "Inv", "Inv", "F_Add", "F_Mul", "F_Min", "F_Max", "A_Add", "A_Mul", "A_Min_S", "A_Max_S", "Inv", "Inv", "A_Min_U", "A_Max_U"};
    string coll_type [4] = {"Unicast", "Multicast", "Parallel Reduction", "Offload Reduction"};
    string directions [5] = {"N","E","S","W","L"};
    string flit_Type [10] = {"N-AW","N-W ","N-AR","W-AR","N-B ","N-R ","W-B ","W-AW","W-W ","W-R "};
    int end_loop = Eject + int'(LOG_LOCAL_PORT);

    initial begin
        automatic string combined_message;
        automatic string temp_message;
        automatic string temp_input_message;
        automatic string temp_output_message;

        automatic bit f_send_message;

        while(1) begin
            combined_message = "";
            f_send_message = 1'b0;

            @(posedge clk_i);

            // Log all input request's
            if(LOG_INPUT_COLL_OPERATION) begin
                temp_input_message = "";
                temp_message = "";
                // Narrow request
                for(int out = North; out < end_loop;out++) begin
                    if(LOG_NARROW_REQUEST) begin
                        if(LOG_VIRTUAL_NARROW == 1'b0) begin
                            if((floo_req_in[out].valid) && (floo_req_out[out].ready)) begin
                                unique case(floo_req_in[out].req.generic.hdr.commtype)
                                    Multicast : temp_message = {temp_message, "(MC) Type ",flit_Type[floo_req_in[out].req.generic.hdr.axi_ch] , " Dir [", directions[out],"] "};
                                    ParallelReduction : temp_message = {temp_message, "(PR) Type ",flit_Type[floo_req_in[out].req.generic.hdr.axi_ch] , " Dir [", directions[out],"] OP [",op_codes_parallel[floo_req_in[out].req.generic.hdr.reduction_op],"] "};
                                    OffloadReduction : temp_message = {temp_message, "(OR) Type ",flit_Type[floo_req_in[out].req.generic.hdr.axi_ch] , " Dir [", directions[out],"] OP [",op_codes_offload[floo_req_in[out].req.generic.hdr.reduction_op],"] "};
                                    default:;
                                endcase
                            end
                        end else begin
                            if((floo_req_in[out].valid[1]) && (floo_req_out[out].ready[1])) begin
                                unique case(floo_req_in[out].req.generic.hdr.commtype)
                                    Multicast : temp_message = {temp_message, "(MC) Type ",flit_Type[floo_req_in[out].req.generic.hdr.axi_ch] , " Dir [", directions[out],"] "};
                                    ParallelReduction : temp_message = {temp_message, "(PR) Type ",flit_Type[floo_req_in[out].req.generic.hdr.axi_ch] , " Dir [", directions[out],"] OP [",op_codes_parallel[floo_req_in[out].req.generic.hdr.reduction_op],"] "};
                                    OffloadReduction : temp_message = {temp_message, "(OR) Type ",flit_Type[floo_req_in[out].req.generic.hdr.axi_ch] , " Dir [", directions[out],"] OP [",op_codes_offload[floo_req_in[out].req.generic.hdr.reduction_op],"] "};
                                    default:;
                                endcase
                            end
                        end
                    end
                end
                
                if(temp_message != "") begin
                    temp_input_message = {temp_input_message, "# REQ N: ", temp_message};
                    f_send_message = 1'b1;
                    temp_message = "";
                end
                
                // Narrow response
                for(int out = North; out < end_loop;out++) begin
                    if(LOG_NARROW_RESPOND) begin
                        if((floo_rsp_in[out].valid) && (floo_rsp_out[out].ready)) begin
                            unique case(floo_rsp_in[out].rsp.generic.hdr.commtype)
                                Multicast : temp_message = {temp_message, "(MC) Type ",flit_Type[floo_rsp_in[out].rsp.generic.hdr.axi_ch] , " Dir [", directions[out],"] "};
                                ParallelReduction : temp_message = {temp_message, "(PR) Type ",flit_Type[floo_rsp_in[out].rsp.generic.hdr.axi_ch] , " Dir [", directions[out], "] OP [",op_codes_parallel[floo_rsp_in[out].rsp.generic.hdr.reduction_op],"] "};
                                OffloadReduction : temp_message = {temp_message, "(OR) Type ",flit_Type[floo_rsp_in[out].rsp.generic.hdr.axi_ch] , " Dir [", directions[out], "] OP [",op_codes_offload[floo_rsp_in[out].rsp.generic.hdr.reduction_op],"] "};
                                default:;
                            endcase
                        end
                    end
                end
                
                if(temp_message != "") begin
                    temp_input_message = {temp_input_message, "# RSP N: ", temp_message};
                    f_send_message = 1'b1;
                    temp_message = "";
                end
                
                // Wide request
                for(int out = North; out < end_loop;out++) begin
                    if(LOG_WIDE_REQUEST) begin
                        if(LOG_VIRTUAL_WIDE == 1'b0) begin
                            if((floo_wide_in[out].valid) && (floo_wide_out[out].ready)) begin
                                unique case(floo_wide_in[out].wide.generic.hdr.commtype)
                                    Multicast : temp_message = {temp_message, "(MC) Type ",flit_Type[floo_wide_in[out].wide.generic.hdr.axi_ch] , " Dir [", directions[out],"] "};
                                    ParallelReduction : temp_message = {temp_message, "(PR) Type ",flit_Type[floo_wide_in[out].wide.generic.hdr.axi_ch] , " Dir [", directions[out],"] OP [",op_codes_parallel[floo_wide_in[out].wide.generic.hdr.reduction_op],"] "};
                                    OffloadReduction : temp_message = {temp_message, "(OR) Type ",flit_Type[floo_wide_in[out].wide.generic.hdr.axi_ch] , " Dir [", directions[out],"] OP [",op_codes_offload[floo_wide_in[out].wide.generic.hdr.reduction_op],"] "};
                                    default:;
                                endcase
                            end
                        end else begin
                            if((floo_wide_in[out].valid[1]) && (floo_wide_out[out].ready[1])) begin
                                unique case(floo_wide_in[out].wide.generic.hdr.commtype)
                                    Multicast : temp_message = {temp_message, "(MC) Type ",flit_Type[floo_wide_in[out].wide.generic.hdr.axi_ch] , " Dir [", directions[out],"] "};
                                    ParallelReduction : temp_message = {temp_message, "(PR) Type ",flit_Type[floo_wide_in[out].wide.generic.hdr.axi_ch] , " Dir [", directions[out],"] OP [",op_codes_parallel[floo_wide_in[out].wide.generic.hdr.reduction_op],"] "};
                                    OffloadReduction : temp_message = {temp_message, "(OR) Type ",flit_Type[floo_wide_in[out].wide.generic.hdr.axi_ch] , " Dir [", directions[out],"] OP [",op_codes_offload[floo_wide_in[out].wide.generic.hdr.reduction_op],"] "};
                                    default:;
                                endcase
                            end
                        end
                    end
                end
                
                if(temp_message != "") begin
                    temp_input_message = {temp_input_message, "# REQ W: ", temp_message};
                    f_send_message = 1'b1;
                    temp_message = "";
                end

                // Merge all input req to the output message
                if(temp_input_message != "") begin
                    combined_message = {combined_message, "*** IN ***  ", temp_input_message};
                end
            end

            // Log all output request's
            if(LOG_OUTPUT_COLL_OPERATION) begin
                temp_output_message = "";
                temp_message = "";
                // Narrow request
                for(int out = North; out < end_loop;out++) begin
                    if(LOG_NARROW_REQUEST) begin
                        if(LOG_VIRTUAL_NARROW == 1'b0) begin
                            if((floo_req_out[out].valid) && (floo_req_in[out].ready)) begin
                                unique case(floo_req_out[out].req.generic.hdr.commtype)
                                    Multicast : temp_message = {temp_message, "(MC) Type ",flit_Type[floo_req_out[out].req.generic.hdr.axi_ch] , " Dir [", directions[out],"] "};
                                    ParallelReduction : temp_message = {temp_message, "(PR) Type ",flit_Type[floo_req_out[out].req.generic.hdr.axi_ch] , " Dir [", directions[out],"] OP [",op_codes_parallel[floo_req_out[out].req.generic.hdr.reduction_op],"] "};
                                    OffloadReduction : temp_message = {temp_message, "(OR) Type ",flit_Type[floo_req_out[out].req.generic.hdr.axi_ch] , " Dir [", directions[out],"] OP [",op_codes_offload[floo_req_out[out].req.generic.hdr.reduction_op],"] "};
                                    default:;
                                endcase
                            end
                        end else begin
                            if((floo_req_out[out].valid[1]) && (floo_req_in[out].ready[1])) begin
                                unique case(floo_req_out[out].req.generic.hdr.commtype)
                                    Multicast : temp_message = {temp_message, "(MC) Type ",flit_Type[floo_req_out[out].req.generic.hdr.axi_ch] , " Dir [", directions[out],"] "};
                                    ParallelReduction : temp_message = {temp_message, "(PR) Type ",flit_Type[floo_req_out[out].req.generic.hdr.axi_ch] , " Dir [", directions[out],"] OP [",op_codes_parallel[floo_req_out[out].req.generic.hdr.reduction_op],"] "};
                                    OffloadReduction : temp_message = {temp_message, "(OR) Type ",flit_Type[floo_req_out[out].req.generic.hdr.axi_ch] , " Dir [", directions[out],"] OP [",op_codes_offload[floo_req_out[out].req.generic.hdr.reduction_op],"] "};
                                    default:;
                                endcase
                            end
                        end
                    end
                end
                
                if(temp_message != "") begin
                    temp_output_message = {temp_output_message, "# REQ N: ", temp_message};
                    f_send_message = 1'b1;
                    temp_message = "";
                end
                
                // Narrow response
                for(int out = North; out < end_loop;out++) begin
                    if(LOG_NARROW_RESPOND) begin
                        if((floo_rsp_out[out].valid) && (floo_rsp_in[out].ready)) begin
                            unique case(floo_rsp_out[out].rsp.generic.hdr.commtype)
                                Multicast : temp_message = {temp_message, "(MC) Type ",flit_Type[floo_rsp_out[out].rsp.generic.hdr.axi_ch] , " Dir [", directions[out],"] "};
                                ParallelReduction : temp_message = {temp_message, "(PR) Type ",flit_Type[floo_rsp_out[out].rsp.generic.hdr.axi_ch] , " Dir [", directions[out], "] OP [",op_codes_parallel[floo_rsp_out[out].rsp.generic.hdr.reduction_op],"] "};
                                OffloadReduction : temp_message = {temp_message, "(OR) Type ",flit_Type[floo_rsp_out[out].rsp.generic.hdr.axi_ch] , " Dir [", directions[out], "] OP [",op_codes_offload[floo_rsp_out[out].rsp.generic.hdr.reduction_op],"] "};
                                default:;
                            endcase
                        end
                    end
                end
                
                if(temp_message != "") begin
                    temp_output_message = {temp_output_message, "# RSP N: ", temp_message};
                    f_send_message = 1'b1;
                    temp_message = "";
                end
                
                // Wide request
                for(int out = North; out < end_loop;out++) begin
                    if(LOG_WIDE_REQUEST) begin
                        if(LOG_VIRTUAL_WIDE == 1'b0) begin
                            if((floo_wide_out[out].valid) && (floo_wide_in[out].ready)) begin
                                unique case(floo_wide_out[out].wide.generic.hdr.commtype)
                                    Multicast : temp_message = {temp_message, "(MC) Type ",flit_Type[floo_wide_out[out].wide.generic.hdr.axi_ch] , " Dir [", directions[out],"] "};
                                    ParallelReduction : temp_message = {temp_message, "(PR) Type ",flit_Type[floo_wide_out[out].wide.generic.hdr.axi_ch] , " Dir [", directions[out],"] OP [",op_codes_parallel[floo_wide_out[out].wide.generic.hdr.reduction_op],"] "};
                                    OffloadReduction : temp_message = {temp_message, "(OR) Type ",flit_Type[floo_wide_out[out].wide.generic.hdr.axi_ch] , " Dir [", directions[out],"] OP [",op_codes_offload[floo_wide_out[out].wide.generic.hdr.reduction_op],"] "};
                                    default:;
                                endcase
                            end
                        end else begin
                            if((floo_wide_out[out].valid[1]) && (floo_wide_in[out].ready[1])) begin
                                unique case(floo_wide_out[out].wide.generic.hdr.commtype)
                                    Multicast : temp_message = {temp_message, "(MC) Type ",flit_Type[floo_wide_out[out].wide.generic.hdr.axi_ch] , " Dir [", directions[out],"] "};
                                    ParallelReduction : temp_message = {temp_message, "(PR) Type ",flit_Type[floo_wide_out[out].wide.generic.hdr.axi_ch] , " Dir [", directions[out],"] OP [",op_codes_parallel[floo_wide_out[out].wide.generic.hdr.reduction_op],"] "};
                                    OffloadReduction : temp_message = {temp_message, "(OR) Type ",flit_Type[floo_wide_out[out].wide.generic.hdr.axi_ch] , " Dir [", directions[out],"] OP [",op_codes_offload[floo_wide_out[out].wide.generic.hdr.reduction_op],"] "};
                                    default:;
                                endcase
                            end
                        end
                    end
                end
                
                if(temp_message != "") begin
                    temp_output_message = {temp_output_message, "# REQ W: ", temp_message};
                    f_send_message = 1'b1;
                    temp_message = "";
                end

                // Merge all input req to the output message
                if(temp_output_message != "") begin
                    combined_message = {combined_message, "*** OUT *** ", temp_output_message};
                end
            end

            // Plot the data
            if(f_send_message) begin
                $display($time, " %s Tile (x:[%1d] y:[%1d]) Route   > %s", tile_type, id_i.x, id_i.y, combined_message);
            end
        end
    end
    
    `endif
endmodule


module picobello_offload_logger
    import cheshire_pkg::*;
    import floo_pkg::*;
    import floo_picobello_noc_pkg::*;
    import picobello_pkg::*;
#(
    // Log all collective operation (reduction + multicast) which enters the router
    parameter bit LOG_OFFLOAD_OPERATION     = 1'b0,
    // Float or Double
    parameter bit DATA_DOUBLE               = 1'b0,
    // data type
    parameter type data_t                   = logic,
    // Nam of the tile
    parameter string tile_type              = "Test"
) (
    input logic clk_i,
    // Chimney ports
    input id_t id_i,
    // Offload port
    input data_t [1:0]                      offload_req_operands,
    input reduction_op_e                    offload_req_operation,
    input logic                             offload_req_valid,
    input logic                             offload_req_ready,
    input data_t                            offload_resp_result,
    input logic                             offload_resp_valid,
    input logic                             offload_resp_ready
);
    // Disable the synth for these modules!
    `ifndef SYNTHESIS

    // Type of collective operation
    string op_codes_offload [16] = {"R_Select", "Inv", "Inv", "Inv", "F_Add", "F_Mul", "F_Min", "F_Max", "A_Add", "A_Mul", "A_Min_S", "A_Max_S", "Inv", "Inv", "A_Min_U", "A_Max_U"};

    initial begin
        automatic string combined_message;
        automatic bit f_send_message;

        while(1) begin
            combined_message = "";
            f_send_message = 1'b0;

            @(posedge clk_i);

            // Print the request here
            if(offload_req_valid && offload_req_ready) begin
                f_send_message = 1'b1;
                if(DATA_DOUBLE) begin
                    combined_message = {combined_message, "# REQ: ", op_codes_offload[offload_req_operation], " Data [ ", $sformatf("%f", $bitstoreal(offload_req_operands[1][63:0])), " , ", $sformatf("%f", $bitstoreal(offload_req_operands[0][63:0])), " ] "};
                end else begin
                    combined_message = {combined_message, "# REQ: ", op_codes_offload[offload_req_operation], " Data [ ", $sformatf("%d", offload_req_operands[1][63:0]), " , ", $sformatf("%d", offload_req_operands[0][63:0]), " ] "};
                end
            end

            // Print the response here
            if(offload_resp_valid && offload_resp_ready) begin
                f_send_message = 1'b1;
                if(DATA_DOUBLE) begin
                    combined_message = {combined_message, "# RESP: Data [ ", $sformatf("%f", $bitstoreal(offload_resp_result[63:0])), " ] "};
                end else begin
                    combined_message = {combined_message, "# RESP: Data [ ", $sformatf("%d", offload_resp_result[63:0]), " ] "};
                end
            end

            // Plot the data
            if(f_send_message) begin
                $display($time, " %s Tile (x:[%1d] y:[%1d]) Offload > %s", tile_type, id_i.x, id_i.y, combined_message);
            end
        end
    end

    `endif
endmodule