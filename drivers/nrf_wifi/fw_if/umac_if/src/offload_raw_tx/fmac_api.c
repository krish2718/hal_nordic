/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @brief File containing API definitions for the
 * FMAC IF Layer of the Wi-Fi driver.
 */

#include "host_rpu_umac_if.h"
#include "fmac_api.h"
#include "hal_api.h"
#include "fmac_structs.h"
#include "fmac_api.h"
#include "fmac_util.h"
#include "fmac_peer.h"
#include "fmac_vif.h"
#include "fmac_tx.h"
#include "fmac_rx.h"
#include "fmac_cmd.h"
#include "fmac_event.h"
#include "fmac_bb.h"
#include "util.h"

struct nrf_wifi_fmac_priv *nrf_wifi_fmac_init_offloaded_raw_tx(void)
{
	struct nrf_wifi_fmac_priv *fpriv = NULL;
	struct nrf_wifi_hal_cfg_params hal_cfg_params;

	fpriv = nrf_wifi_osal_mem_zalloc(sizeof(*fpriv));
	if (!fpriv) {
		nrf_wifi_osal_log_err("%s: Unable to allocate fpriv",
				      __func__);
		goto out;
	}

	nrf_wifi_osal_mem_set(&hal_cfg_params,
			      0,
			      sizeof(hal_cfg_params));

	hal_cfg_params.max_cmd_size = MAX_NRF_WIFI_UMAC_CMD_SIZE;
	hal_cfg_params.max_event_size = MAX_EVENT_POOL_LEN;

	fpriv->hpriv = nrf_wifi_hal_init(&hal_cfg_params,
					 &nrf_wifi_fmac_event_callback,
					 NULL);
	if (!fpriv->hpriv) {
		nrf_wifi_osal_log_err("%s: Unable to do HAL init",
				      __func__);
		nrf_wifi_osal_mem_free(fpriv);
		fpriv = NULL;
		goto out;
	}
out:
	return fpriv;
}

static enum nrf_wifi_status nrf_wifi_fmac_fw_init_offloaded_raw_tx(
		struct nrf_wifi_fmac_dev_ctx *fmac_dev_ctx,
		struct nrf_wifi_phy_rf_params *rf_params,
		bool rf_params_valid,
#ifdef NRF_WIFI_LOW_POWER
		int sleep_type,
#endif /* NRF_WIFI_LOW_POWER */
		unsigned int phy_calib,
		enum op_band op_band,
		bool beamforming,
		struct nrf_wifi_tx_pwr_ctrl_params *tx_pwr_ctrl,
		struct nrf_wifi_board_params *board_params)
{
	unsigned long start_time_us = 0;
	enum nrf_wifi_status status = NRF_WIFI_STATUS_FAIL;

	if (!fmac_dev_ctx) {
		nrf_wifi_osal_log_err("%s: Invalid device context",
				      __func__);
		goto out;
	}

	status = umac_cmd_init(fmac_dev_ctx,
			       rf_params,
			       rf_params_valid,
#ifdef NRF_WIFI_LOW_POWER
			       sleep_type,
#endif /* NRF_WIFI_LOW_POWER */
			       phy_calib,
			       op_band,
			       beamforming,
			       tx_pwr_ctrl,
			       board_params);

	if (status != NRF_WIFI_STATUS_SUCCESS) {
		nrf_wifi_osal_log_err("%s: UMAC init failed",
				      __func__);
		goto out;
	}

	start_time_us = nrf_wifi_osal_time_get_curr_us();
	while (!fmac_dev_ctx->fw_init_done) {
		nrf_wifi_osal_sleep_ms(1);
#define MAX_INIT_WAIT (5 * 1000 * 1000)
		if (nrf_wifi_osal_time_elapsed_us(start_time_us) >= MAX_INIT_WAIT) {
			break;
		}
	}

	if (!fmac_dev_ctx->fw_init_done) {
		nrf_wifi_osal_log_err("%s: UMAC init timed out",
				      __func__);
		status = NRF_WIFI_STATUS_FAIL;
		goto out;
	}

	status = NRF_WIFI_STATUS_SUCCESS;
out:
	return status;
}

enum nrf_wifi_status nrf_wifi_fmac_dev_init_offloaded_raw_tx(
		struct nrf_wifi_fmac_dev_ctx *fmac_dev_ctx,
#ifdef NRF_WIFI_LOW_POWER
		int sleep_type,
#endif /* NRF_WIFI_LOW_POWER */
		unsigned int phy_calib,
		enum op_band op_band,
		bool beamforming,
		struct nrf_wifi_tx_pwr_ctrl_params *tx_pwr_ctrl_params,
		struct nrf_wifi_tx_pwr_ceil_params *tx_pwr_ceil_params,
		struct nrf_wifi_board_params *board_params)
{
	enum nrf_wifi_status status = NRF_WIFI_STATUS_FAIL;
	struct nrf_wifi_fmac_otp_info otp_info;
	struct nrf_wifi_phy_rf_params phy_rf_params;

	if (!fmac_dev_ctx) {
		nrf_wifi_osal_log_err("%s: Invalid device context",
				      __func__);
		goto out;
	}

	status = nrf_wifi_hal_dev_init(fmac_dev_ctx->hal_dev_ctx);
	if (status != NRF_WIFI_STATUS_SUCCESS) {
		nrf_wifi_osal_log_err("%s: nrf_wifi_hal_dev_init failed",
				      __func__);
		goto out;
	}

	fmac_dev_ctx->tx_pwr_ceil_params = nrf_wifi_osal_mem_alloc(sizeof(*tx_pwr_ceil_params));
	nrf_wifi_osal_mem_cpy(fmac_dev_ctx->tx_pwr_ceil_params,
			      tx_pwr_ceil_params,
			      sizeof(*tx_pwr_ceil_params));

	nrf_wifi_osal_mem_set(&otp_info,
			      0xFF,
			      sizeof(otp_info));

	status = nrf_wifi_hal_otp_info_get(fmac_dev_ctx->hal_dev_ctx,
					   &otp_info.info,
					   &otp_info.flags);
	if (status != NRF_WIFI_STATUS_SUCCESS) {
		nrf_wifi_osal_log_err("%s: Fetching of RPU OTP information failed",
				      __func__);
		goto out;
	}

	status = nrf_wifi_fmac_fw_init_offloaded_raw_tx(
			fmac_dev_ctx,
			&phy_rf_params,
			true,
#ifdef NRF_WIFI_LOW_POWER
			sleep_type,
#endif /* NRF_WIFI_LOW_POWER */
			phy_calib,
			op_band,
			beamforming,
			tx_pwr_ctrl_params,
			board_params);

	if (status == NRF_WIFI_STATUS_FAIL) {
		nrf_wifi_osal_log_err("%s: nrf_wifi_fmac_fw_init failed",
				      __func__);
		goto out;
	}
out:
	return status;
}

void nrf_wifi_fmac_dev_rem_offloaded_raw_tx(struct nrf_wifi_fmac_dev_ctx *fmac_dev_ctx)
{
	nrf_wifi_hal_dev_rem(fmac_dev_ctx->hal_dev_ctx);
	nrf_wifi_osal_mem_free(fmac_dev_ctx);
}

static void nrf_wifi_fmac_fw_deinit_offloaded_raw_tx(struct nrf_wifi_fmac_dev_ctx *fmac_dev_ctx)
{
}

void nrf_wifi_fmac_dev_deinit_offloaded_raw_tx(struct nrf_wifi_fmac_dev_ctx *fmac_dev_ctx)
{
	nrf_wifi_osal_mem_free(fmac_dev_ctx->tx_pwr_ceil_params);
	nrf_wifi_fmac_fw_deinit_offloaded_raw_tx(fmac_dev_ctx);
}

void nrf_wifi_fmac_deinit_offloaded_raw_tx(struct nrf_wifi_fmac_priv *fpriv)
{
	nrf_wifi_hal_deinit(fpriv->hpriv);
	nrf_wifi_osal_mem_free(fpriv);
}
