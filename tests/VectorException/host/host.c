// Copyright (c) Open Enclave SDK contributors.
// Licensed under the MIT License.
#include <limits.h>
#include <openenclave/host.h>
#include <openenclave/internal/error.h>
#include <openenclave/internal/tests.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../host/sgx/cpuid.h"
#include "VectorException_u.h"

#define SKIP_RETURN_CODE 2

static bool _was_ocall_called = false;

void host_set_was_ocall_called()
{
    _was_ocall_called = true;
}

void test_vector_exception(
    oe_enclave_t* enclave,
    int use_exception_handler_stack)
{
    int ret = -1;
    oe_result_t result =
        enc_test_vector_exception(enclave, &ret, use_exception_handler_stack);

    if (result != OE_OK)
    {
        oe_put_err("enc_test_vector_exception() failed: result=%u", result);
    }

    if (ret != 0)
    {
        oe_put_err("enc_test_vector_exception failed ret=%d", ret);
    }

    OE_TEST(ret == 0);
}

void test_ocall_in_handler(
    oe_enclave_t* enclave,
    int use_exception_handler_stack)
{
    int ret = -1;
    oe_result_t result =
        enc_test_ocall_in_handler(enclave, &ret, use_exception_handler_stack);

    if (result != OE_OK)
    {
        oe_put_err("enc_test_ocall_in_handler() failed: result=%u", result);
    }

    OE_TEST(ret == 0);
    OE_TEST(_was_ocall_called == true);
    _was_ocall_called = false;
}

void test_sigill_handling(
    oe_enclave_t* enclave,
    int use_exception_handler_stack)
{
    uint32_t cpuid_table[OE_CPUID_LEAF_COUNT][OE_CPUID_REG_COUNT];
    memset(&cpuid_table, 0, sizeof(cpuid_table));
    int ret = -1;

    oe_result_t result = enc_test_sigill_handling(
        enclave, &ret, use_exception_handler_stack, cpuid_table);
    if (result != OE_OK)
    {
        oe_put_err("enc_test_sigill_handling() failed: result=%u", result);
    }

    if (ret != 0)
    {
        oe_put_err("enc_test_sigill_handling failed ret=%d", ret);
    }

    OE_TEST(ret == 0);

    // Verify that the enclave cached CPUID values match host's
    // First, verify values being tested do not reach above max supported leaf.
    uint32_t cpuid_maxlevel[OE_CPUID_REG_COUNT];
    memset(cpuid_maxlevel, 0, sizeof(cpuid_maxlevel));
    oe_get_cpuid(
        0,
        0,
        &cpuid_maxlevel[OE_CPUID_RAX],
        &cpuid_maxlevel[OE_CPUID_RBX],
        &cpuid_maxlevel[OE_CPUID_RCX],
        &cpuid_maxlevel[OE_CPUID_RDX]);

    if (OE_CPUID_LEAF_COUNT - 1 > cpuid_maxlevel[OE_CPUID_RAX])
    {
        oe_put_err(
            "Test machine does not support CPUID leaf %x expected by "
            "test_sigill_handling.\n",
            (OE_CPUID_LEAF_COUNT - 1));
    }

    // Check all values.
    for (uint32_t i = 0; i < OE_CPUID_LEAF_COUNT; i++)
    {
        uint32_t leaf = supported_cpuid_leaves[i];
        if (!oe_is_emulated_cpuid_leaf(leaf))
        {
            continue;
        }

        uint32_t cpuid_info[OE_CPUID_REG_COUNT];
        memset(cpuid_info, 0, sizeof(cpuid_info));
        oe_get_cpuid(
            leaf,
            0,
            &cpuid_info[OE_CPUID_RAX],
            &cpuid_info[OE_CPUID_RBX],
            &cpuid_info[OE_CPUID_RCX],
            &cpuid_info[OE_CPUID_RDX]);

        for (uint32_t j = 0; j < OE_CPUID_REG_COUNT; j++)
        {
            if (leaf == 0 && j == OE_CPUID_RAX)
            {
                // The enclave sets this to the highest emulated leaf.
                OE_TEST(OE_CPUID_MAX_BASIC == cpuid_table[i][j]);
            }
            else if (leaf == 1 && j == OE_CPUID_RBX)
            {
                // The highest 8 bits indicates the current executing processor
                // id.
                // There is no guarantee that the value is the same across
                // multiple cpu-id calls since the thread could be scheduled to
                // different processors for different calls.
                // Additionally, the enclave returns a cached value which has
                // lesser chance of matching up with the current value.
                OE_TEST(
                    (cpuid_info[j] & 0x00FFFFFF) ==
                    (cpuid_table[i][j] & 0x00FFFFFF));
            }
            else if (leaf == 0x80000000 && j == OE_CPUID_RAX)
            {
                // The enclave sets this to the highest emulated leaf.
                OE_TEST(OE_CPUID_MAX_EXTENDED == cpuid_table[i][j]);
            }
            else
            {
                OE_TEST(cpuid_info[j] == cpuid_table[i][j]);
            }
        }
    }
}

int main(int argc, const char* argv[])
{
    oe_result_t result;
    oe_enclave_t* enclave = NULL;

    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s ENCLAVE_PATH\n", argv[0]);
        return 1;
    }

    printf("=== This program is used to test basic vector exception "
           "functionalities.\n");

    const uint32_t flags = oe_get_create_flags();
    if ((flags & OE_ENCLAVE_FLAG_SIMULATE) != 0)
    {
        printf("=== Skipped unsupported test in simulation mode "
               "(VectorException)\n");
        return SKIP_RETURN_CODE;
    }

    if ((result = oe_create_VectorException_enclave(
             argv[1], OE_ENCLAVE_TYPE_SGX, flags, NULL, 0, &enclave)) != OE_OK)
    {
        oe_put_err("oe_create_VectorException_enclave(): result=%u", result);
    }

    OE_TEST(enc_test_cpuid_in_global_constructors(enclave) == OE_OK);

    /* Test with the default behavior (using stack pointer stored in SSA) */
    test_vector_exception(enclave, 0);
    test_sigill_handling(enclave, 0);
    test_ocall_in_handler(enclave, 0);

    /* Test with setting an exception handler stack */
    test_vector_exception(enclave, 1);
    test_sigill_handling(enclave, 1);
    test_ocall_in_handler(enclave, 1);

    oe_terminate_enclave(enclave);

    printf("=== passed all tests (VectorException)\n");

    return 0;
}
