/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 <your name here>
 * Based on the Yokogawa DLM driver by abraxa (Soeren Apel)
 * and the Hameg HMO driver by poljar (Damir Jelić).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <stdlib.h>
#include "scpi.h"
#include "protocol.h"

static struct sr_dev_driver hp1660es_driver_info;

/*
 * Manufacturer ID dall'IDN.
 * Quirk: alcuni HP 1660ES rispondono "HEWLETT PACKARD" (con spazio),
 * altri "HEWLETT-PACKARD" (con trattino). Gestiamo entrambi.
 */
static const char * const MANUFACTURER_IDS[] = {
	"HEWLETT PACKARD",
	"HEWLETT-PACKARD",
	"HP",
	NULL
};

/* ─────────────────────────────────────────────────────────────────────────────
 * Opzioni driver
 * ──────────────────────────────────────────────────────────────────────────── */

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
	SR_CONF_OSCILLOSCOPE,
};

/*
 * Opzioni device globali (non legate a un channel group specifico).
 */
static const uint32_t devopts[] = {
	SR_CONF_LIMIT_FRAMES    | SR_CONF_GET | SR_CONF_SET,
	/* LA samplerate (TIMING mode) */
	SR_CONF_SAMPLERATE      | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	/* LA STATE mode — clock source e setup/hold */
	SR_CONF_EXTERNAL_CLOCK_SOURCE | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_CAPTURE_RATIO   | SR_CONF_GET | SR_CONF_SET,
	/* Timebase OSC */
	SR_CONF_TIMEBASE        | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_NUM_HDIV        | SR_CONF_GET,
	SR_CONF_HORIZ_TRIGGERPOS | SR_CONF_GET | SR_CONF_SET,
	/* Trigger */
	SR_CONF_TRIGGER_SOURCE  | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SLOPE   | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_LEVEL   | SR_CONF_GET | SR_CONF_SET,
	/* ACQuire */
	SR_CONF_AVERAGING       | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_AVG_SAMPLES     | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_PEAK_DETECTION  | SR_CONF_GET | SR_CONF_SET,
};

/* Opzioni per channel group analogici OSC.
 * I parametri globali OSC (timebase, trigger, ecc.) sono inclusi qui
 * oltre che in devopts[], così sigrok-cli li accetta anche quando
 * un channel group analogico è attivo sulla riga di comando. */
static const uint32_t devopts_cg_analog[] = {
	SR_CONF_NUM_VDIV        | SR_CONF_GET,
	SR_CONF_VDIV            | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_COUPLING        | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OFFSET          | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_PROBE_FACTOR    | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TIMEBASE        | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_HORIZ_TRIGGERPOS | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_SOURCE  | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SLOPE   | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_LEVEL   | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_AVERAGING       | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_AVG_SAMPLES     | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_PEAK_DETECTION  | SR_CONF_GET | SR_CONF_SET,
};

/*
 * Opzioni per channel group digitali LA.
 * SR_CONF_LOGIC_THRESHOLD per soglia per-pod (THRESHOLD1..8).
 */
static const uint32_t devopts_cg_digital[] = {
	SR_CONF_LOGIC_THRESHOLD | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

enum {
	CG_INVALID = -1,
	CG_NONE,
	CG_ANALOG,
	CG_DIGITAL,
};

/* ─────────────────────────────────────────────────────────────────────────────
 * probe_device
 * Interroga *IDN?, verifica il modello, costruisce l'istanza device.
 * ──────────────────────────────────────────────────────────────────────────── */

static struct sr_dev_inst *probe_device(struct sr_scpi_dev_inst *scpi)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_scpi_hw_info *hw_info;
	char model_name[32];
	int i;
	gboolean manufacturer_ok;

	sdi      = NULL;
	devc     = NULL;
	hw_info  = NULL;

	if (sr_scpi_get_hw_id(scpi, &hw_info) != SR_OK) {
		sr_info("HP1660ES: couldn't get IDN response.");
		goto fail;
	}

	/*
	 * Verifica manufacturer — accetta "HEWLETT PACKARD", "HEWLETT-PACKARD", "HP".
	 * Quirk: il 1660ES risponde "HEWLETT PACKARD" (con spazio, senza trattino).
	 */
	manufacturer_ok = FALSE;
	for (i = 0; MANUFACTURER_IDS[i]; i++) {
		if (g_ascii_strncasecmp(hw_info->manufacturer,
				MANUFACTURER_IDS[i],
				strlen(MANUFACTURER_IDS[i])) == 0) {
			manufacturer_ok = TRUE;
			break;
		}
	}

	if (!manufacturer_ok) {
		sr_dbg("HP1660ES: manufacturer '%s' not HP, skipping.",
				hw_info->manufacturer);
		goto fail;
	}

	/* Prefix matching bidirezionale sul model ID */
	if (hp1660es_model_match(hw_info->model, model_name,
			sizeof(model_name)) != SR_OK)
		goto fail;

	sr_info("HP1660ES: found %s (model_id='%s', fw='%s')",
			model_name, hw_info->model,
			hw_info->firmware_version ? hw_info->firmware_version : "?");

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->vendor  = g_strdup("HP");
	sdi->model   = g_strdup(model_name);
	sdi->version = g_strdup(hw_info->firmware_version
			? hw_info->firmware_version : "");

	devc = g_malloc0(sizeof(struct dev_context));
	sdi->driver    = &hp1660es_driver_info;
	sdi->priv      = devc;
	sdi->inst_type = SR_INST_SCPI;
	sdi->conn      = scpi;

	sr_scpi_hw_info_free(hw_info);
	hw_info = NULL;

	/* Inizializza strutture interne + registra canali */
	if (hp1660es_device_init(sdi) != SR_OK)
		goto fail;

	/*
	 * Aggiorna model_id e firmware_ver nella config statica.
	 * (hp1660es_device_init imposta i default — qui sovrascriviamo
	 * con i valori reali dall'IDN)
	 */
	{
		struct dev_context *d = sdi->priv;
		struct scope_config *cfg = (struct scope_config *)d->model_config;
		/* hw_info è già stato liberato — usiamo sdi->model e sdi->version */
		g_strlcpy(cfg->model_name,   model_name,   sizeof(cfg->model_name));
		g_strlcpy(cfg->firmware_ver, sdi->version, sizeof(cfg->firmware_ver));
	}

	return sdi;

fail:
	sr_scpi_hw_info_free(hw_info);
	sr_dev_inst_free(sdi);
	g_free(devc);
	return NULL;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	return sr_scpi_scan(di->context, options, probe_device);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * dev_clear / clear_helper
 * ──────────────────────────────────────────────────────────────────────────── */

static void clear_helper(struct dev_context *devc)
{
	hp1660es_scope_state_destroy(devc->model_state);
	g_free(devc->analog_groups);
	g_free(devc->digital_groups);
	g_free(devc->blob_buf);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear_with_callback(di,
			(std_dev_clear_callback)clear_helper);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * dev_open / dev_close
 * ──────────────────────────────────────────────────────────────────────────── */

static int dev_open(struct sr_dev_inst *sdi)
{
	if (sr_scpi_open(sdi->conn) != SR_OK)
		return SR_ERR;

	/*
	 * Legge lo stato corrente dell'hardware.
	 * Quirk: *CLS deve venire prima di SELECT per evitare errore -212.
	 * hp1660es_scope_state_query() gestisce già questa sequenza.
	 */
	if (hp1660es_scope_state_query(sdi) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	return sr_scpi_close(sdi->conn);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * check_channel_group
 * Determina il tipo di channel group: CG_NONE, CG_ANALOG, CG_DIGITAL.
 * ──────────────────────────────────────────────────────────────────────────── */

static int check_channel_group(const struct dev_context *devc,
		const struct sr_channel_group *cg)
{
	const struct scope_config *model;

	if (!devc)
		return CG_INVALID;

	model = devc->model_config;

	if (!cg)
		return CG_NONE;

	if (std_cg_idx(cg, devc->analog_groups,
			model->analog_channels) >= 0)
		return CG_ANALOG;

	if (std_cg_idx(cg, devc->digital_groups,
			model->pods) >= 0)
		return CG_DIGITAL;

	sr_err("HP1660ES: invalid channel group specified.");
	return CG_INVALID;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * config_get
 * ──────────────────────────────────────────────────────────────────────────── */

static int config_get(uint32_t key, GVariant **data,
		const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	int ret, cg_type, idx;
	struct dev_context *devc;
	const struct scope_config *model;
	struct scope_state *state;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	if ((cg_type = check_channel_group(devc, cg)) == CG_INVALID)
		return SR_ERR;

	model = devc->model_config;
	state = devc->model_state;
	ret   = SR_OK;

	switch (key) {

	/* ── Globali ── */

	case SR_CONF_LIMIT_FRAMES:
		*data = g_variant_new_uint64(devc->frame_limit);
		break;

	case SR_CONF_SAMPLERATE:
		/*
		 * In base al tipo di acquisizione corrente ritorniamo
		 * il samplerate LA o OSC.
		 * Se nessuna acquisizione in corso, ritorniamo LA samplerate
		 * (il più informativo per un LA combo).
		 */
		*data = g_variant_new_uint64(state->sample_rate
				? state->sample_rate
				: state->la_samplerate_hz);
		break;

	case SR_CONF_TIMEBASE:
		*data = g_variant_new("(tt)",
				hp1660es_osc_timebases[state->osc_timebase_idx][0],
				hp1660es_osc_timebases[state->osc_timebase_idx][1]);
		break;

	case SR_CONF_NUM_HDIV:
		*data = g_variant_new_int32(model->num_xdivs);
		break;

	case SR_CONF_HORIZ_TRIGGERPOS:
		*data = g_variant_new_double(state->osc_horiz_triggerpos);
		break;

	case SR_CONF_TRIGGER_SOURCE:
		*data = g_variant_new_string(state->osc_trigger_source);
		break;

	case SR_CONF_TRIGGER_SLOPE:
		*data = g_variant_new_string(state->osc_trigger_slope);
		break;

	case SR_CONF_TRIGGER_LEVEL:
		*data = g_variant_new_double(state->osc_trigger_level_v);
		break;

	/* ── Channel group analogico ── */

	case SR_CONF_NUM_VDIV:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_ANALOG)
			return SR_ERR_NA;
		*data = g_variant_new_int32(model->num_ydivs);
		break;

	case SR_CONF_VDIV:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_ANALOG)
			return SR_ERR_NA;
		idx = std_cg_idx(cg, devc->analog_groups,
				model->analog_channels);
		if (idx < 0)
			return SR_ERR_ARG;
		*data = g_variant_new("(tt)",
				hp1660es_osc_vdivs[state->analog_states[idx].vdiv_idx][0],
				hp1660es_osc_vdivs[state->analog_states[idx].vdiv_idx][1]);
		break;

	case SR_CONF_COUPLING:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_ANALOG)
			return SR_ERR_NA;
		idx = std_cg_idx(cg, devc->analog_groups,
				model->analog_channels);
		if (idx < 0)
			return SR_ERR_ARG;
		*data = g_variant_new_string(
				(*model->coupling_options)[state->analog_states[idx].coupling]);
		break;

	case SR_CONF_OFFSET:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_ANALOG)
			return SR_ERR_NA;
		idx = std_cg_idx(cg, devc->analog_groups,
				model->analog_channels);
		if (idx < 0)
			return SR_ERR_ARG;
		*data = g_variant_new_double(state->analog_states[idx].vertical_offset);
		break;

	case SR_CONF_PROBE_FACTOR:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_ANALOG)
			return SR_ERR_NA;
		idx = std_cg_idx(cg, devc->analog_groups,
				model->analog_channels);
		if (idx < 0)
			return SR_ERR_ARG;
		*data = g_variant_new_uint64(state->analog_states[idx].probe_factor);
		break;

	case SR_CONF_AVERAGING:
		*data = g_variant_new_boolean(
				g_ascii_strcasecmp(state->osc_acq_type, "AVERage") == 0);
		break;

	case SR_CONF_AVG_SAMPLES:
		*data = g_variant_new_uint64(state->osc_acq_count > 0
				? (uint64_t)state->osc_acq_count : 1);
		break;

	case SR_CONF_PEAK_DETECTION:
		*data = g_variant_new_boolean(
				g_ascii_strcasecmp(state->osc_acq_type, "PEAK") == 0);
		break;

	/* ── Channel group digitale ── */

	case SR_CONF_LOGIC_THRESHOLD:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_DIGITAL)
			return SR_ERR_NA;
		idx = std_cg_idx(cg, devc->digital_groups, model->pods);
		if (idx < 0)
			return SR_ERR_ARG;
		*data = g_variant_new_double(state->pod_states[idx].threshold_v);
		break;

	/* ── STATE mode parameters ── */
	case SR_CONF_EXTERNAL_CLOCK_SOURCE:
		/* Restituisce il clock source corrente per STATE mode.
		 * Formato: "<clock_id>,<clock_spec>" es. "J,RISing" */
		{
			char cs_str[16];
			g_snprintf(cs_str, sizeof(cs_str), "%s,%s",
				state->la_state_clock_id,
				state->la_state_clock_spec);
			*data = g_variant_new_string(cs_str);
		}
		break;

	case SR_CONF_CAPTURE_RATIO:
		/* Riuso SR_CONF_CAPTURE_RATIO per esporre la_state_sethold.
		 * 0..9 → valori tabella 15-2 del manuale HP 1660E. */
		*data = g_variant_new_uint64((uint64_t)state->la_state_sethold);
		break;

	default:
		ret = SR_ERR_NA;
		break;
	}

	return ret;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * config_set
 * ──────────────────────────────────────────────────────────────────────────── */

static int config_set(uint32_t key, GVariant *data,
		const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	int ret, cg_type, idx, j;
	struct dev_context *devc;
	const struct scope_config *model;
	struct scope_state *state;
	struct sr_scpi_dev_inst *scpi;
	char cmd[128];
	double tmp_d;
	float vdiv, range;

	if (!sdi || !(devc = sdi->priv))
		return SR_ERR_ARG;

	model = devc->model_config;
	state = devc->model_state;
	scpi  = sdi->conn;

	/*
	 * Primo switch: parametri globali.
	 * Gestiti con return diretto, prima del check sul channel group.
	 * Questo permette a sigrok-cli di passarli anche quando ha un
	 * channel group attivo sulla riga di comando (es. dopo
	 * --channel-group OSC1), senza ricevere SR_ERR_NA.
	 */
	switch (key) {

	case SR_CONF_LIMIT_FRAMES:
		devc->frame_limit = g_variant_get_uint64(data);
		return SR_OK;

	case SR_CONF_SAMPLERATE:
		/*
		 * Imposta LA samplerate.
		 * Cerca nella tabella hp1660es_la_samplerates il valore più vicino.
		 */
		{
			uint64_t sr = g_variant_get_uint64(data);
			int best = 0;
			uint64_t best_err = UINT64_MAX;
			int i;
			for (i = 0; i < HP1660ES_NUM_LA_SAMPLERATES; i++) {
				uint64_t tbl_sr = hp1660es_la_samplerates[i][1] /
						hp1660es_la_samplerates[i][0];
				uint64_t err = (tbl_sr > sr) ? (tbl_sr - sr) : (sr - tbl_sr);
				if (err < best_err) {
					best_err = err;
					best = i;
				}
			}
			state->la_samplerate_idx = best;
			state->la_samplerate_hz  = hp1660es_la_samplerates[best][1] /
					hp1660es_la_samplerates[best][0];
			state->la_sample_period_ps =
					1000000000000ULL / state->la_samplerate_hz;
			return SR_OK;
		}

	case SR_CONF_TIMEBASE:
		{
			int tidx = std_u64_tuple_idx(data,
					ARRAY_AND_SIZE(hp1660es_osc_timebases));
			if (tidx < 0)
				return SR_ERR_ARG;
			state->osc_timebase_idx = tidx;
			{
				float tdiv = (float)hp1660es_osc_timebases[tidx][0] /
						(float)hp1660es_osc_timebases[tidx][1];
				float tb_range = tdiv * model->num_xdivs;
				g_snprintf(cmd, sizeof(cmd), ":TIMebase:RANGe %g", tb_range);
				return sr_scpi_send(scpi, cmd);
			}
		}

	case SR_CONF_HORIZ_TRIGGERPOS:
		{
			double hpos = g_variant_get_double(data);
			if (hpos < 0.0 || hpos > 1.0)
				return SR_ERR_ARG;
			state->osc_horiz_triggerpos = (float)hpos;
			/*
			 * TIMebase:DELay: offset in secondi rispetto al centro.
			 * delay = -(pos - 0.5) * timebase_range
			 */
			{
				float tdiv = (float)hp1660es_osc_timebases[state->osc_timebase_idx][0] /
						(float)hp1660es_osc_timebases[state->osc_timebase_idx][1];
				float tb_range = tdiv * model->num_xdivs;
				float delay = -(float)(hpos - 0.5) * tb_range;
				g_snprintf(cmd, sizeof(cmd), ":TIMebase:DELay %g", delay);
				return sr_scpi_send(scpi, cmd);
			}
		}

	case SR_CONF_TRIGGER_SOURCE:
		{
			const char *src = g_variant_get_string(data, NULL);
			gboolean found = FALSE;
			int i;
			for (i = 0; i < model->num_trigger_sources; i++) {
				if (g_ascii_strcasecmp(src,
						(*model->trigger_sources)[i]) == 0) {
					found = TRUE;
					break;
				}
			}
			if (!found)
				return SR_ERR_ARG;
			g_strlcpy(state->osc_trigger_source, src,
					sizeof(state->osc_trigger_source));
			g_snprintf(cmd, sizeof(cmd), ":TRIGger:SOURce %s", src);
			return sr_scpi_send(scpi, cmd);
		}

	case SR_CONF_TRIGGER_SLOPE:
		{
			const char *slope = g_variant_get_string(data, NULL);
			g_strlcpy(state->osc_trigger_slope, slope,
					sizeof(state->osc_trigger_slope));
			g_snprintf(cmd, sizeof(cmd), ":TRIGger:SLOPe %s", slope);
			return sr_scpi_send(scpi, cmd);
		}

	case SR_CONF_TRIGGER_LEVEL:
		{
			double tlev = g_variant_get_double(data);
			state->osc_trigger_level_v = (float)tlev;
			g_snprintf(cmd, sizeof(cmd), ":TRIGger:LEVel %g", tlev);
			return sr_scpi_send(scpi, cmd);
		}

	case SR_CONF_AVERAGING:
		{
			gboolean avg = g_variant_get_boolean(data);
			if (avg) {
				g_strlcpy(state->osc_acq_type, "AVERage",
						sizeof(state->osc_acq_type));
				if (state->osc_acq_count < 2)
					state->osc_acq_count = 2;
				g_snprintf(cmd, sizeof(cmd), ":ACQuire:TYPE AVERage");
			} else {
				g_strlcpy(state->osc_acq_type, "NORMal",
						sizeof(state->osc_acq_type));
				g_snprintf(cmd, sizeof(cmd), ":ACQuire:TYPE NORMal");
			}
			return sr_scpi_send(scpi, cmd);
		}

	case SR_CONF_AVG_SAMPLES:
		{
			uint64_t count = g_variant_get_uint64(data);
			if (count < 2 || count > 4096)
				return SR_ERR_ARG;
			state->osc_acq_count = (int)count;
			if (g_ascii_strcasecmp(state->osc_acq_type, "AVERage") != 0) {
				g_strlcpy(state->osc_acq_type, "AVERage",
						sizeof(state->osc_acq_type));
				if (sr_scpi_send(scpi, ":ACQuire:TYPE AVERage") != SR_OK)
					return SR_ERR;
			}
			g_snprintf(cmd, sizeof(cmd), ":ACQuire:COUNt %"PRIu64, count);
			return sr_scpi_send(scpi, cmd);
		}

	case SR_CONF_PEAK_DETECTION:
		{
			gboolean peak = g_variant_get_boolean(data);
			if (peak) {
				g_strlcpy(state->osc_acq_type, "PEAK",
						sizeof(state->osc_acq_type));
				g_snprintf(cmd, sizeof(cmd), ":ACQuire:TYPE PEAK");
			} else {
				g_strlcpy(state->osc_acq_type, "NORMal",
						sizeof(state->osc_acq_type));
				g_snprintf(cmd, sizeof(cmd), ":ACQuire:TYPE NORMal");
			}
			return sr_scpi_send(scpi, cmd);
		}

	default:
		break;  /* parametro per-group — continua */
	}

	/*
	 * Secondo switch: parametri per channel group.
	 * Da qui in poi il cg è rilevante.
	 */
	if ((cg_type = check_channel_group(devc, cg)) == CG_INVALID)
		return SR_ERR;

	ret = SR_OK;

	switch (key) {

	/* ── Channel group analogico ── */

	case SR_CONF_VDIV:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_ANALOG)
			return SR_ERR_NA;
		idx = std_u64_tuple_idx(data,
				ARRAY_AND_SIZE(hp1660es_osc_vdivs));
		if (idx < 0)
			return SR_ERR_ARG;
		j = std_cg_idx(cg, devc->analog_groups,
				model->analog_channels);
		if (j < 0)
			return SR_ERR_ARG;
		state->analog_states[j].vdiv_idx = idx;
		vdiv  = (float)hp1660es_osc_vdivs[idx][0] /
				(float)hp1660es_osc_vdivs[idx][1];
		range = vdiv * model->num_ydivs;
		g_snprintf(cmd, sizeof(cmd), ":CHANnel%d:RANGe %g", j + 1, range);
		if (sr_scpi_send(scpi, cmd) != SR_OK)
			return SR_ERR;
		ret = sr_scpi_get_opc(scpi);
		break;

	case SR_CONF_COUPLING:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_ANALOG)
			return SR_ERR_NA;
		idx = std_str_idx(data, *model->coupling_options,
				model->num_coupling_options);
		if (idx < 0)
			return SR_ERR_ARG;
		j = std_cg_idx(cg, devc->analog_groups,
				model->analog_channels);
		if (j < 0)
			return SR_ERR_ARG;
		state->analog_states[j].coupling = idx;
		g_snprintf(cmd, sizeof(cmd), ":CHANnel%d:COUPling %s",
				j + 1, (*model->coupling_options)[idx]);
		if (sr_scpi_send(scpi, cmd) != SR_OK)
			return SR_ERR;
		ret = sr_scpi_get_opc(scpi);
		break;

	case SR_CONF_OFFSET:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_ANALOG)
			return SR_ERR_NA;
		j = std_cg_idx(cg, devc->analog_groups,
				model->analog_channels);
		if (j < 0)
			return SR_ERR_ARG;
		tmp_d = g_variant_get_double(data);
		state->analog_states[j].waveform_offset  = (float)tmp_d;
		state->analog_states[j].vertical_offset  = (float)tmp_d;
		g_snprintf(cmd, sizeof(cmd), ":CHANnel%d:OFFSet %g", j + 1, tmp_d);
		if (sr_scpi_send(scpi, cmd) != SR_OK)
			return SR_ERR;
		ret = sr_scpi_get_opc(scpi);
		break;

	case SR_CONF_PROBE_FACTOR:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_ANALOG)
			return SR_ERR_NA;
		j = std_cg_idx(cg, devc->analog_groups,
				model->analog_channels);
		if (j < 0)
			return SR_ERR_ARG;
		{
			uint64_t factor = g_variant_get_uint64(data);
			if (factor != 1 && factor != 10)
				return SR_ERR_ARG;
			state->analog_states[j].probe_factor = (int)factor;
			g_snprintf(cmd, sizeof(cmd), ":CHANnel%d:PROBe %"PRIu64,
					j + 1, factor);
		}
		if (sr_scpi_send(scpi, cmd) != SR_OK)
			return SR_ERR;
		ret = sr_scpi_get_opc(scpi);
		break;

	/* ── Channel group digitale ── */

	case SR_CONF_LOGIC_THRESHOLD:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_DIGITAL)
			return SR_ERR_NA;
		idx = std_cg_idx(cg, devc->digital_groups, model->pods);
		if (idx < 0)
			return SR_ERR_ARG;
		tmp_d = g_variant_get_double(data);
		state->pod_states[idx].threshold_v      = (float)tmp_d;
		state->pod_states[idx].threshold_preset = FALSE;
		state->pod_states[idx].threshold_idx    = -1;
		/*
		 * THRESHOLD1..8 — uno per pod A..H.
		 * Verificato su hardware REV 02.02.
		 */
		g_snprintf(cmd, sizeof(cmd),
				":MACHINE1:TFORMAT:THRESHOLD%d %g",
				idx + 1, tmp_d);
		ret = sr_scpi_send(scpi, cmd);
		break;

	/* ── STATE mode parameters ── */
	case SR_CONF_EXTERNAL_CLOCK_SOURCE:
		/*
		 * Imposta clock source per STATE mode.
		 * Formato atteso: "<clock_id>,<clock_spec>"
		 * Esempi: "J,RISing"  "K,FALLing"  "J,BOTH"
		 * clock_id  : {J|K|L|M|N|P}
		 * clock_spec: {RISing|FALLing|BOTH|OFF}
		 */
		{
			const char *cs_val = g_variant_get_string(data, NULL);
			char cs_id[4] = {0};
			char cs_spec[8] = {0};
			if (sscanf(cs_val, "%3[^,],%7s", cs_id, cs_spec) == 2) {
				g_strlcpy(state->la_state_clock_id,
					cs_id, sizeof(state->la_state_clock_id));
				g_strlcpy(state->la_state_clock_spec,
					cs_spec, sizeof(state->la_state_clock_spec));
				g_strlcpy(state->la_machine_type, "STATE",
					sizeof(state->la_machine_type));
				sr_info("HP1660ES: STATE mode set — clock %s,%s",
					state->la_state_clock_id,
					state->la_state_clock_spec);
			} else {
				sr_err("HP1660ES: invalid clock source '%s' "
					"(expected '<id>,<spec>' e.g. 'J,RISing')",
					cs_val);
				ret = SR_ERR_ARG;
			}
		}
		break;

	case SR_CONF_CAPTURE_RATIO:
		/*
		 * Imposta setup/hold per STATE mode (tabella 15-2 manuale).
		 * Valori 0..9:
		 *   0 = 3.5/0.0 ns  (default)
		 *   7 = 0.0/3.5 ns
		 * (con clock singolo e un edge)
		 */
		{
			uint64_t sh_val = g_variant_get_uint64(data);
			if (sh_val > 9) {
				sr_err("HP1660ES: sethold value %"PRIu64
					" out of range (0..9)", sh_val);
				ret = SR_ERR_ARG;
			} else {
				state->la_state_sethold = (int)sh_val;
				sr_info("HP1660ES: STATE sethold set to %d",
					state->la_state_sethold);
			}
		}
		break;

	default:
		ret = SR_ERR_NA;
		break;
	}

	return ret;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * config_channel_set
 * Abilita/disabilita un canale.
 * ──────────────────────────────────────────────────────────────────────────── */

static int config_channel_set(const struct sr_dev_inst *sdi,
		struct sr_channel *ch, unsigned int changes)
{
	struct dev_context *devc;
	struct scope_state *state;
	char cmd[64];

	if (changes != SR_CHANNEL_SET_ENABLED)
		return SR_ERR_NA;

	devc  = sdi->priv;
	state = devc->model_state;

	if (ch->type == SR_CHANNEL_ANALOG) {
		int ch_idx = ch->index - HP1660ES_ANALOG_CHAN_INDEX_OFFS;
		if (ch_idx < 0 || ch_idx >= HP1660ES_N_ANALOG_CHANNELS)
			return SR_ERR_ARG;
		state->analog_states[ch_idx].state = ch->enabled;
		/* Non esiste un comando esplicito per abilitare/disabilitare
		 * i canali OSC sul 1660ES — lo stato è implicito nel configure. */
		return SR_OK;
	}

	if (ch->type == SR_CHANNEL_LOGIC) {
		int ch_idx = ch->index - HP1660ES_DIG_CHAN_INDEX_OFFS;
		if (ch_idx < 0 || ch_idx >= HP1660ES_N_DIG_CHANNELS)
			return SR_ERR_ARG;
		state->digital_states[ch_idx] = ch->enabled;

		/*
		 * Aggiorna stato pod.
		 * Un pod è abilitato se almeno un suo canale è abilitato.
		 */
		{
			int pod_idx = ch_idx / HP1660ES_BITS_PER_POD;
			gboolean pod_on = FALSE;
			int i;
			for (i = pod_idx * HP1660ES_BITS_PER_POD;
			     i < (pod_idx + 1) * HP1660ES_BITS_PER_POD; i++) {
				if (state->digital_states[i]) {
					pod_on = TRUE;
					break;
				}
			}
			state->pod_states[pod_idx].enabled = pod_on;
		}

		(void)cmd;
		return SR_OK;
	}

	return SR_ERR_NA;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * config_list
 * ──────────────────────────────────────────────────────────────────────────── */

static int config_list(uint32_t key, GVariant **data,
		const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	int cg_type = CG_NONE;
	struct dev_context *devc;
	const struct scope_config *model;

	devc  = sdi ? sdi->priv : NULL;
	model = devc ? devc->model_config : NULL;

	if (!cg) {
		switch (key) {
		case SR_CONF_SCAN_OPTIONS:
		case SR_CONF_DEVICE_OPTIONS:
			return STD_CONFIG_LIST(key, data, sdi, cg,
					scanopts, drvopts, devopts);

		case SR_CONF_TIMEBASE:
			*data = std_gvar_tuple_array(
					ARRAY_AND_SIZE(hp1660es_osc_timebases));
			return SR_OK;

		case SR_CONF_SAMPLERATE:
			/* Lista samplerate LA */
			{
				uint64_t rates[HP1660ES_NUM_LA_SAMPLERATES];
				int i;
				for (i = 0; i < HP1660ES_NUM_LA_SAMPLERATES; i++)
					rates[i] = hp1660es_la_samplerates[i][1] /
							hp1660es_la_samplerates[i][0];
				*data = std_gvar_samplerates(ARRAY_AND_SIZE(rates));
			}
			return SR_OK;

		case SR_CONF_TRIGGER_SOURCE:
			if (!model)
				return SR_ERR_ARG;
			*data = g_variant_new_strv(*model->trigger_sources,
					model->num_trigger_sources);
			return SR_OK;

		case SR_CONF_TRIGGER_SLOPE:
			*data = g_variant_new_strv(
					(const char *[]){ "POSitive", "NEGative" }, 2);
			return SR_OK;

		case SR_CONF_AVG_SAMPLES:
			/* Potenze di 2 da 2 a 4096 */
			*data = std_gvar_array_u64(
					(const uint64_t[]){ 2, 4, 8, 16, 32, 64, 128, 256,
					                    512, 1024, 2048, 4096 }, 12);
			return SR_OK;

		case SR_CONF_NUM_HDIV:
			if (!model)
				return SR_ERR_ARG;
			*data = g_variant_new_int32(model->num_xdivs);
			return SR_OK;

		default:
			return SR_ERR_NA;
		}
	}

	if ((cg_type = check_channel_group(devc, cg)) == CG_INVALID)
		return SR_ERR;

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		if (cg_type == CG_ANALOG) {
			*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg_analog));
			break;
		}
		if (cg_type == CG_DIGITAL) {
			*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg_digital));
			break;
		}
		*data = std_gvar_array_u32(NULL, 0);
		break;

	case SR_CONF_VDIV:
		if (cg_type != CG_ANALOG)
			return SR_ERR_NA;
		*data = std_gvar_tuple_array(ARRAY_AND_SIZE(hp1660es_osc_vdivs));
		break;

	case SR_CONF_COUPLING:
		if (cg_type != CG_ANALOG)
			return SR_ERR_NA;
		if (!model)
			return SR_ERR_ARG;
		*data = g_variant_new_strv(*model->coupling_options,
				model->num_coupling_options);
		break;

	case SR_CONF_PROBE_FACTOR:
		if (cg_type != CG_ANALOG)
			return SR_ERR_NA;
		*data = std_gvar_array_u64(
				(const uint64_t[]){ 1, 10 }, 2);
		break;

	/* Parametri globali OSC — rispondono anche quando cg è attivo */
	case SR_CONF_TIMEBASE:
		if (cg_type != CG_ANALOG)
			return SR_ERR_NA;
		*data = std_gvar_tuple_array(ARRAY_AND_SIZE(hp1660es_osc_timebases));
		break;

	case SR_CONF_TRIGGER_SOURCE:
		if (cg_type != CG_ANALOG)
			return SR_ERR_NA;
		if (!model)
			return SR_ERR_ARG;
		*data = g_variant_new_strv(*model->trigger_sources,
				model->num_trigger_sources);
		break;

	case SR_CONF_TRIGGER_SLOPE:
		if (cg_type != CG_ANALOG)
			return SR_ERR_NA;
		*data = g_variant_new_strv(
				(const char *[]){ "POSitive", "NEGative" }, 2);
		break;

	case SR_CONF_AVG_SAMPLES:
		if (cg_type != CG_ANALOG)
			return SR_ERR_NA;
		*data = std_gvar_array_u64(
				(const uint64_t[]){ 2, 4, 8, 16, 32, 64, 128, 256,
				                    512, 1024, 2048, 4096 }, 12);
		break;

	case SR_CONF_LOGIC_THRESHOLD:
		if (cg_type != CG_DIGITAL)
			return SR_ERR_NA;
		/*
		 * Lista threshold preset: TTL (1.5V), ECL (-1.3V), CMOS (2.5V).
		 * PulseView mostra questi come opzioni selezionabili.
		 */
		*data = g_variant_new_strv(
				(const char *[]){ "TTL", "ECL", "CMOS" }, 3);
		break;

	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * dev_acquisition_start
 *
 * Determina il tipo di acquisizione in base ai canali abilitati:
 * - Solo logici     → ACQ_TYPE_LA   (SELECT 1, SYSTEM:DATA?)
 * - Solo analogici  → ACQ_TYPE_OSC  (SELECT 2, WAVeform:DATA?)
 * - Entrambi        → ACQ_TYPE_MIXED (cross-arming LA→OSC via INTermodule)
 * ──────────────────────────────────────────────────────────────────────────── */

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	GSList *l;
	struct sr_channel *ch;
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;
	gboolean has_logic, has_analog;

	scpi = sdi->conn;
	devc = sdi->priv;

	/* Costruisce la lista dei canali abilitati */
	g_slist_free(devc->enabled_channels);
	devc->enabled_channels = NULL;

	has_logic = has_analog = FALSE;
	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (!ch->enabled)
			continue;
		devc->enabled_channels = g_slist_append(
			devc->enabled_channels, ch);
		if (ch->type == SR_CHANNEL_LOGIC)
			has_logic = TRUE;
		if (ch->type == SR_CHANNEL_ANALOG)
			has_analog = TRUE;
	}

	if (!devc->enabled_channels) {
		sr_err("HP1660ES: no channels enabled.");
		return SR_ERR;
	}

	/* Verifica combinazione canali */
	if (hp1660es_check_channels(devc->enabled_channels) != SR_OK)
		return SR_ERR_NA;

	/* Determina tipo acquisizione */
	if (has_logic && has_analog)
		devc->acq_type = ACQ_TYPE_MIXED;
	else if (has_logic)
		devc->acq_type = ACQ_TYPE_LA;
	else
		devc->acq_type = ACQ_TYPE_OSC;

	/*
	 * Disabilita esplicitamente tutti i canali non in enabled_channels.
	 * Questo fa sì che output/srzip scriva nel metadata solo i canali
	 * effettivamente usati, con unitsize coerente.
	 * Warning se i canali logici abilitati superano 64 — limite PulseView.
	 */
	if (has_logic) {
		int n_logic_enabled = 0;

		for (l = sdi->channels; l; l = l->next) {
			ch = l->data;
			if (ch->type == SR_CHANNEL_LOGIC)
				ch->enabled = FALSE;
		}

		for (l = devc->enabled_channels; l; l = l->next) {
			ch = l->data;
			if (ch->type == SR_CHANNEL_LOGIC) {
				ch->enabled = TRUE;
				n_logic_enabled++;
			}
		}

		if (n_logic_enabled > 64)
			sr_warn("HP1660ES: %d logic channels enabled — "
				"PulseView may crash with more than 64 channels.",
				n_logic_enabled);
	}

	devc->current_channel = devc->enabled_channels;
	devc->num_frames = 0;
	devc->blob_buf = NULL;
	devc->blob_len = 0;
	devc->blob_expected = 0;

	if (devc->acq_type == ACQ_TYPE_LA) {
		/* la_configure legge SPERIOD → la_samplerate_hz noto prima
		 * dell'header, così il metadata .sr ha il samplerate corretto. */
		if (hp1660es_la_configure(sdi) != SR_OK)
			return SR_ERR;
		std_session_send_df_header(sdi);
		if (hp1660es_la_acquire(sdi) != SR_OK)
			return SR_ERR;

		sr_scpi_source_add(sdi->session, scpi, G_IO_IN, 50,
			hp1660es_la_data_receive, (void *)sdi);

	} else if (devc->acq_type == ACQ_TYPE_OSC) {
		if (hp1660es_osc_configure(sdi) != SR_OK)
			return SR_ERR;

		sr_scpi_source_add(sdi->session, scpi, G_IO_IN, 50,
			hp1660es_osc_data_receive, (void *)sdi);

		if (hp1660es_osc_acquire(sdi) != SR_OK)
			return SR_ERR;

	} else if (devc->acq_type == ACQ_TYPE_MIXED) {
		/*
		 * Acquisizione mixed LA+OSC (cross-arming via INTermodule):
		 * 1. mixed_configure(): configura LA, OSC con IMMEDIATE, INSert tree
		 * 2. mixed_acquire(): RMODE SINGLE + START
		 * 3. mixed_data_receive(): polling OPC, download LA, download OSC
		 */
		if (hp1660es_mixed_configure(sdi) != SR_OK)
			return SR_ERR;

		/* mixed_configure include la_configure → la_samplerate_hz noto */
		std_session_send_df_header(sdi);

		sr_scpi_source_add(sdi->session, scpi, G_IO_IN, 50,
			hp1660es_mixed_data_receive, (void *)sdi);

		if (hp1660es_mixed_acquire(sdi) != SR_OK)
			return SR_ERR;

	} else {
		sr_err("HP1660ES: unknown acquisition type %d.", devc->acq_type);
		return SR_ERR;
	}

	return SR_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * dev_acquisition_stop
 * ──────────────────────────────────────────────────────────────────────────── */

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;

	std_session_send_df_end(sdi);

	devc = sdi->priv;
	scpi = sdi->conn;

	if (devc->acq_type == ACQ_TYPE_MIXED)
		sr_scpi_send(scpi, ":INTermodule:DELete ALL");

	devc->num_frames = 0;
	devc->acq_state = ACQ_STATE_IDLE;

	g_free(devc->blob_buf);
	devc->blob_buf = NULL;
	devc->blob_len = 0;
	devc->blob_expected = 0;
	devc->blob_allocated = 0;

	g_slist_free(devc->enabled_channels);
	devc->enabled_channels = NULL;

	sr_scpi_source_remove(sdi->session, sdi->conn);

	return SR_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Driver info struct
 * ──────────────────────────────────────────────────────────────────────────── */

static struct sr_dev_driver hp1660es_driver_info = {
	.name                 = "hp1660es",
	.longname             = "HP 1660E/ES/EP series",
	.api_version          = 1,
	.init                 = std_init,
	.cleanup              = std_cleanup,
	.scan                 = scan,
	.dev_list             = std_dev_list,
	.dev_clear            = dev_clear,
	.config_get           = config_get,
	.config_set           = config_set,
	.config_channel_set   = config_channel_set,
	.config_list          = config_list,
	.dev_open             = dev_open,
	.dev_close            = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop  = dev_acquisition_stop,
	.context              = NULL,
};
SR_REGISTER_DEV_DRIVER(hp1660es_driver_info);
