// Copyright (C) 2018-2023 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "test_utils.h"

#include <intel_gpu/primitives/input_layout.hpp>
#include "intel_gpu/primitives/concatenation.hpp"
#include <intel_gpu/primitives/data.hpp>

using namespace cldnn;
using namespace ::tests;

/*
    This set of tests has been designed to check the correctness of trim_to_outputs optimization pass
*/

class trim_to_outputs: public ::testing::Test {
public:
    /*
    In this test we check if the convolution conv2 will be eliminated from the network. This is expected to be done in trim_to_outputs optimization pass

    Network structure:  input  -> conv1 (output)
                            \
                                ---> conv2 (to be eliminated)
    */
    void test_one_node_to_eliminate_case1(bool is_caching_test) {
        auto& engine = get_test_engine();
        ExecutionConfig config = get_test_default_config(engine);
        config.set_property(ov::intel_gpu::custom_outputs(std::vector<std::string>{ "conv1" }));
        config.set_property(ov::intel_gpu::optimize_data(false));             // to avoid adding reorders

        auto input = engine.allocate_memory({ data_types::f32, format::yxfb, { 1, 1, 1, 1 } });
        auto weights = engine.allocate_memory({ data_types::f32, format::bfyx, { 1, 1, 1, 1 } });
        auto bias = engine.allocate_memory({ data_types::f32, format::bfyx, { 1, 1, 1, 1 } });

        set_values(input, { 1.1f });
        set_values(weights, { 2.1f });
        set_values(bias, { 1.6f });

        std::vector<float> out_data = { 3.91f };

        topology topology;
        topology.add(input_layout("input", input->get_layout()));
        topology.add(data("weights", weights));
        topology.add(data("bias", bias));
        topology.add(cldnn::convolution("conv1", { input_info("input") }, { "weights" }, { "bias" }));
        topology.add(cldnn::convolution("conv2", { input_info("input") }, { "weights" }, { "bias" }));

        cldnn::network::ptr network = get_network(engine, topology, config, get_test_stream_ptr(), is_caching_test);
        network->set_input_data("input", input);
        auto outputs = network->execute();

        ASSERT_EQ(outputs.size(), (size_t)1); // there is only one output
        ASSERT_EQ(network->get_executed_primitives().size(), (size_t)2);   // input and conv1 where executed
        ASSERT_EQ(network->get_all_primitive_ids().size(), (size_t)4);     // also bias and weights still exist

        for (auto& it : outputs)
        {
            cldnn::mem_lock<float> output_ptr(it.second.get_memory(), get_test_stream());
            for (size_t cntr = 0; cntr < out_data.size(); cntr++)
            {
                ASSERT_NEAR(output_ptr[cntr], out_data[cntr], 1e-4);
            }
            ASSERT_EQ(it.first, "conv1");
        }
    }

    /*
    in this test we check if the convolution conv2 will be eliminated from the network. This is expected to be done in trim_to_outputs optimization pass

    Network structure:  input  -> conv1 (output)
                            \
                            ---> conv2 (to be eliminated along with its weights and bias)
    */
    void test_one_node_to_eliminate_case2(bool is_caching_test) {
        auto& engine = get_test_engine();
        ExecutionConfig config = get_test_default_config(engine);
        config.set_property(ov::intel_gpu::custom_outputs(std::vector<std::string>{ "conv1" }));
        config.set_property(ov::intel_gpu::optimize_data(false));             // to avoid adding reorders

        auto input = engine.allocate_memory({ data_types::f32, format::yxfb,{ 1, 1, 1, 1 } });
        auto weights1 = engine.allocate_memory({ data_types::f32, format::bfyx,{ 1, 1, 1, 1 } });
        auto weights2 = engine.allocate_memory({ data_types::f32, format::bfyx,{ 1, 1, 1, 1 } });
        auto bias1 = engine.allocate_memory({ data_types::f32, format::bfyx,{ 1, 1, 1, 1 } });
        auto bias2 = engine.allocate_memory({ data_types::f32, format::bfyx,{ 1, 1, 1, 1 } });

        set_values(input, { 1.1f });
        set_values(weights1, { 2.1f });
        set_values(bias1, { 1.6f });
        set_values(weights2, { 0.3f });
        set_values(bias2, { 0.2f });

        std::vector<float> out_data = { 3.91f };

        topology topology;
        topology.add(input_layout("input", input->get_layout()));
        topology.add(data("weights1", weights1));
        topology.add(data("bias1", bias1));
        topology.add(cldnn::convolution("conv1", { input_info("input") }, { "weights1" }, { "bias1" }));
        topology.add(data("weights2", weights2));
        topology.add(data("bias2", bias2));
        topology.add(cldnn::convolution("conv2", { input_info("input") }, { "weights2" }, { "bias2" }));

        cldnn::network::ptr network = get_network(engine, topology, config, get_test_stream_ptr(), is_caching_test);
        network->set_input_data("input", input);
        auto outputs = network->execute();

        ASSERT_EQ(outputs.size(), (size_t)1); // there is only one output
        ASSERT_EQ(network->get_executed_primitives().size(), (size_t)2);   // input and conv1 where executed
        ASSERT_EQ(network->get_all_primitive_ids().size(), (size_t)4);     // also bias1 and weights1 still exist

        for (auto& it : outputs)
        {
            cldnn::mem_lock<float> output_ptr(it.second.get_memory(), get_test_stream());

            for (size_t cntr = 0; cntr < out_data.size(); cntr++)
            {
                ASSERT_NEAR(output_ptr[cntr], out_data[cntr], 1e-4);
            }
            ASSERT_EQ(it.first, "conv1");
        }
    }

    /*
    in this test we check if the convolution conv2 will be eliminated from the network. This is expected to be done in trim_to_outputs optimization pass

    Network structure:  input ---> conv1 --- ---> conv4 (output)
                            \
                            --->  conv2  ---> conv3
    Convolutions conv2, conv3 should be optimized out along with weights23 shered by conv2 and conv3.
    */
    void test_two_nodes_to_eliminate_case1(bool is_caching_test) {
        auto& engine = get_test_engine();
        ExecutionConfig config = get_test_default_config(engine);
        config.set_property(ov::intel_gpu::custom_outputs(std::vector<std::string>{ "conv4" }));
        config.set_property(ov::intel_gpu::optimize_data(false));             // to avoid adding reorders

        auto input = engine.allocate_memory({ data_types::f32, format::yxfb,{ 1, 1, 1, 1 } });
        auto weights1 = engine.allocate_memory({ data_types::f32, format::bfyx,{ 1, 1, 1, 1 } });
        auto weights23 = engine.allocate_memory({ data_types::f32, format::bfyx,{ 1, 1, 1, 1 } });
        auto weights4 = engine.allocate_memory({ data_types::f32, format::bfyx,{ 1, 1, 1, 1 } });
        auto bias = engine.allocate_memory({ data_types::f32, format::bfyx,{ 1, 1, 1, 1 } });

        set_values(input, { 1.1f });
        set_values(weights1, { 2.1f });
        set_values(weights23, { 3.0f });
        set_values(weights4, { 2.0f });
        set_values(bias, { 1.6f });

        std::vector<float> out_data = { 9.42f };

        topology topology;
        topology.add(input_layout("input", input->get_layout()));
        topology.add(data("weights1", weights1));
        topology.add(data("bias", bias));
        topology.add(cldnn::convolution("conv1", { input_info("input") }, { "weights1" }, { "bias" }));
        topology.add(data("weights23", weights23));
        topology.add(cldnn::convolution("conv2", { input_info("input") }, { "weights23" }, { "bias" }));
        topology.add(cldnn::convolution("conv3", { input_info("conv2") }, { "weights23" }, { "bias" }));
        topology.add(data("weights4", weights4));
        topology.add(cldnn::convolution("conv4", { input_info("conv1") }, { "weights4" }, { "bias" }));

        cldnn::network::ptr network = get_network(engine, topology, config, get_test_stream_ptr(), is_caching_test);
        network->set_input_data("input", input);
        auto outputs = network->execute();

        ASSERT_EQ(outputs.size(), (size_t)1); // there is only one output
        ASSERT_EQ(network->get_executed_primitives().size(), (size_t)3);   // input, conv1 and conv4  where executed
        ASSERT_EQ(network->get_all_primitive_ids().size(), (size_t)6);     // also bias weights1 and weights4 still exist

        for (auto& it : outputs)
        {
            cldnn::mem_lock<float> output_ptr(it.second.get_memory(), get_test_stream());

            for (size_t cntr = 0; cntr < out_data.size(); cntr++)
            {
                ASSERT_NEAR(output_ptr[cntr], out_data[cntr], 1e-4);
            }
            ASSERT_EQ(it.first, "conv4");
        }
    }
};

TEST_F(trim_to_outputs, one_node_to_eliminate_case1) {
    this->test_one_node_to_eliminate_case1(false);
}

TEST_F(trim_to_outputs, one_node_to_eliminate_case2) {
    this->test_one_node_to_eliminate_case2(false);
}

TEST_F(trim_to_outputs, two_nodes_to_eliminate_case1) {
    this->test_two_nodes_to_eliminate_case1(false);
}

#ifdef RUN_ALL_MODEL_CACHING_TESTS
TEST_F(trim_to_outputs, one_node_to_eliminate_case1_cached) {
    this->test_one_node_to_eliminate_case1(true);
}

TEST_F(trim_to_outputs, one_node_to_eliminate_case2_cached) {
    this->test_one_node_to_eliminate_case2(true);
}
#endif
TEST_F(trim_to_outputs, two_nodes_to_eliminate_case1_cached) {
    this->test_two_nodes_to_eliminate_case1(true);
}
