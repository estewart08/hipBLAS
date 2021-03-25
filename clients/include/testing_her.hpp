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
hipblasStatus_t testing_her(const Arguments& argus)
{
    using U           = real_t<T>;
    bool FORTRAN      = argus.fortran;
    auto hipblasHerFn = FORTRAN ? hipblasHer<T, U, true> : hipblasHer<T, U, false>;

    int N    = argus.N;
    int incx = argus.incx;
    int lda  = argus.lda;

    int               A_size = lda * N;
    hipblasFillMode_t uplo   = char2hipblas_fill(argus.uplo_option);

    // argument sanity check, quick return if input parameters are invalid before allocating invalid
    // memory
    if(N < 0 || lda < N || incx == 0)
    {
        return HIPBLAS_STATUS_INVALID_VALUE;
    }

    // Naming: dK is in GPU (device) memory. hK is in CPU (host) memory
    host_vector<T> hA(A_size);
    host_vector<T> hA_cpu(A_size);
    host_vector<T> hA_host(A_size);
    host_vector<T> hA_device(A_size);
    host_vector<T> hx(N * incx);

    device_vector<T> dA(A_size);
    device_vector<T> dx(N * incx);
    device_vector<U> d_alpha(1);

    double gpu_time_used, hipblas_error_host, hipblas_error_device;

    U h_alpha = argus.get_alpha<U>();

    hipblasLocalHandle handle(argus);

    // Initial Data on CPU
    srand(1);
    hipblas_init<T>(hA, N, N, lda);
    hipblas_init<T>(hx, 1, N, incx);

    // copy matrix is easy in STL; hA_cpu = hA: save a copy in hA_cpu which will be output of CPU BLAS
    hA_cpu = hA;

    // copy data from CPU to device
    CHECK_HIP_ERROR(hipMemcpy(dA, hA.data(), sizeof(T) * lda * N, hipMemcpyHostToDevice));
    CHECK_HIP_ERROR(hipMemcpy(dx, hx.data(), sizeof(T) * N * incx, hipMemcpyHostToDevice));
    CHECK_HIP_ERROR(hipMemcpy(d_alpha, &h_alpha, sizeof(U), hipMemcpyHostToDevice));

    /* =====================================================================
           HIPBLAS
    =================================================================== */
    CHECK_HIPBLAS_ERROR(hipblasSetPointerMode(handle, HIPBLAS_POINTER_MODE_HOST));
    CHECK_HIPBLAS_ERROR(hipblasHerFn(handle, uplo, N, (U*)&h_alpha, dx, incx, dA, lda));

    CHECK_HIP_ERROR(hipMemcpy(hA_host.data(), dA, sizeof(T) * N * lda, hipMemcpyDeviceToHost));
    CHECK_HIP_ERROR(hipMemcpy(dA, hA.data(), sizeof(T) * N * lda, hipMemcpyHostToDevice));

    CHECK_HIPBLAS_ERROR(hipblasSetPointerMode(handle, HIPBLAS_POINTER_MODE_DEVICE));
    CHECK_HIPBLAS_ERROR(hipblasHerFn(handle, uplo, N, d_alpha, dx, incx, dA, lda));

    CHECK_HIP_ERROR(hipMemcpy(hA_device.data(), dA, sizeof(T) * N * lda, hipMemcpyDeviceToHost));

    if(argus.unit_check || argus.norm_check)
    {
        /* =====================================================================
           CPU BLAS
        =================================================================== */
        cblas_her<T>(uplo, N, h_alpha, hx.data(), incx, hA_cpu.data(), lda);

        // enable unit check, notice unit check is not invasive, but norm check is,
        // unit check and norm check can not be interchanged their order
        if(argus.unit_check)
        {
            unit_check_general<T>(N, N, lda, hA_cpu.data(), hA_host.data());
            unit_check_general<T>(N, N, lda, hA_cpu.data(), hA_device.data());
        }
        if(argus.norm_check)
        {
            hipblas_error_host   = norm_check_general<T>('F', N, N, lda, hA_cpu, hA_host);
            hipblas_error_device = norm_check_general<T>('F', N, N, lda, hA_cpu, hA_device);
        }
    }

    if(argus.timing)
    {
        CHECK_HIP_ERROR(hipMemcpy(dA, hA.data(), sizeof(T) * lda * N, hipMemcpyHostToDevice));
        hipStream_t stream;
        CHECK_HIPBLAS_ERROR(hipblasGetStream(handle, &stream));
        CHECK_HIPBLAS_ERROR(hipblasSetPointerMode(handle, HIPBLAS_POINTER_MODE_DEVICE));

        int runs = argus.cold_iters + argus.iters;
        for(int iter = 0; iter < runs; iter++)
        {
            if(iter == argus.cold_iters)
                gpu_time_used = get_time_us_sync(stream);

            CHECK_HIPBLAS_ERROR(hipblasHerFn(handle, uplo, N, d_alpha, dx, incx, dA, lda));
        }
        gpu_time_used = get_time_us_sync(stream) - gpu_time_used;

        ArgumentModel<e_N, e_alpha, e_incx, e_lda>{}.log_args<U>(std::cout,
                                                                 argus,
                                                                 gpu_time_used,
                                                                 her_gflop_count<T>(N),
                                                                 her_gbyte_count<T>(N),
                                                                 hipblas_error_host,
                                                                 hipblas_error_device);
    }

    return HIPBLAS_STATUS_SUCCESS;
}
