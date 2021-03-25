/* ************************************************************************
 * Copyright 2016-2021 Advanced Micro Devices, Inc.
 *
 * ************************************************************************ */

#include <fstream>
#include <iostream>
#include <stdlib.h>
#include <vector>

#include "testing_common.hpp"

using namespace std;

/* ============================================================================================ */

template <typename T>
hipblasStatus_t testing_her_batched(const Arguments& argus)
{
    using U      = real_t<T>;
    bool FORTRAN = argus.fortran;
    auto hipblasHerBatchedFn
        = FORTRAN ? hipblasHerBatched<T, U, true> : hipblasHerBatched<T, U, false>;

    int N           = argus.N;
    int incx        = argus.incx;
    int lda         = argus.lda;
    int batch_count = argus.batch_count;

    int               A_size = lda * N;
    int               x_size = N * incx;
    hipblasFillMode_t uplo   = char2hipblas_fill(argus.uplo_option);

    double gpu_time_used, hipblas_error_host, hipblas_error_device;

    U h_alpha = argus.get_alpha<U>();

    // argument sanity check, quick return if input parameters are invalid before allocating invalid
    // memory
    if(N < 0 || lda < N || incx == 0 || batch_count < 0)
    {
        return HIPBLAS_STATUS_INVALID_VALUE;
    }
    else if(batch_count == 0)
    {
        return HIPBLAS_STATUS_SUCCESS;
    }

    hipblasLocalHandle handle(argus);

    // Naming: dK is in GPU (device) memory. hK is in CPU (host) memory
    host_batch_vector<T> hA(A_size, 1, batch_count);
    host_batch_vector<T> hA_cpu(A_size, 1, batch_count);
    host_batch_vector<T> hA_host(A_size, 1, batch_count);
    host_batch_vector<T> hA_device(A_size, 1, batch_count);
    host_batch_vector<T> hx(N, incx, batch_count);

    device_batch_vector<T> dA(A_size, 1, batch_count);
    device_batch_vector<T> dx(N, incx, batch_count);
    device_vector<U>       d_alpha(1);

    CHECK_HIP_ERROR(dA.memcheck());
    CHECK_HIP_ERROR(dx.memcheck());

    // Initial Data on CPU
    hipblas_init(hA, true);
    hipblas_init(hx);

    hA_cpu.copy_from(hA);
    CHECK_HIP_ERROR(dA.transfer_from(hA));
    CHECK_HIP_ERROR(dx.transfer_from(hx));
    CHECK_HIP_ERROR(hipMemcpy(d_alpha, &h_alpha, sizeof(U), hipMemcpyHostToDevice));

    /* =====================================================================
           HIPBLAS
    =================================================================== */
    CHECK_HIPBLAS_ERROR(hipblasSetPointerMode(handle, HIPBLAS_POINTER_MODE_HOST));
    CHECK_HIPBLAS_ERROR(hipblasHerBatchedFn(handle,
                                            uplo,
                                            N,
                                            (U*)&h_alpha,
                                            dx.ptr_on_device(),
                                            incx,
                                            dA.ptr_on_device(),
                                            lda,
                                            batch_count));

    CHECK_HIP_ERROR(hA_host.transfer_from(dA));
    CHECK_HIP_ERROR(dA.transfer_from(hA));

    CHECK_HIPBLAS_ERROR(hipblasSetPointerMode(handle, HIPBLAS_POINTER_MODE_DEVICE));
    CHECK_HIPBLAS_ERROR(hipblasHerBatchedFn(
        handle, uplo, N, d_alpha, dx.ptr_on_device(), incx, dA.ptr_on_device(), lda, batch_count));

    CHECK_HIP_ERROR(hA_device.transfer_from(dA));

    if(argus.unit_check || argus.norm_check)
    {
        /* =====================================================================
           CPU BLAS
        =================================================================== */
        for(int b = 0; b < batch_count; b++)
        {
            cblas_her<T>(uplo, N, h_alpha, hx[b], incx, hA_cpu[b], lda);
        }

        // enable unit check, notice unit check is not invasive, but norm check is,
        // unit check and norm check can not be interchanged their order
        if(argus.unit_check)
        {
            unit_check_general<T>(N, N, batch_count, lda, hA_cpu, hA_host);
            unit_check_general<T>(N, N, batch_count, lda, hA_cpu, hA_host);
        }
        if(argus.norm_check)
        {
            hipblas_error_host
                = norm_check_general<T>('F', N, N, lda, hA_cpu, hA_host, batch_count);
            hipblas_error_device
                = norm_check_general<T>('F', N, N, lda, hA_cpu, hA_device, batch_count);
        }
    }

    if(argus.timing)
    {
        CHECK_HIP_ERROR(dA.transfer_from(hA));
        hipStream_t stream;
        CHECK_HIPBLAS_ERROR(hipblasGetStream(handle, &stream));
        CHECK_HIPBLAS_ERROR(hipblasSetPointerMode(handle, HIPBLAS_POINTER_MODE_DEVICE));

        int runs = argus.cold_iters + argus.iters;
        for(int iter = 0; iter < runs; iter++)
        {
            if(iter == argus.cold_iters)
                gpu_time_used = get_time_us_sync(stream);

            CHECK_HIPBLAS_ERROR(hipblasHerBatchedFn(handle,
                                                    uplo,
                                                    N,
                                                    d_alpha,
                                                    dx.ptr_on_device(),
                                                    incx,
                                                    dA.ptr_on_device(),
                                                    lda,
                                                    batch_count));
        }
        gpu_time_used = get_time_us_sync(stream) - gpu_time_used;

        ArgumentModel<e_N, e_alpha, e_incx, e_lda, e_batch_count>{}.log_args<U>(
            std::cout,
            argus,
            gpu_time_used,
            her_gflop_count<T>(N),
            her_gbyte_count<T>(N),
            hipblas_error_host,
            hipblas_error_device);
    }

    return HIPBLAS_STATUS_SUCCESS;
}
