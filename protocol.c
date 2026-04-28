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
#include <inttypes.h>
#include <string.h>
#include <math.h>
#include "scpi.h"
#include "protocol.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Tabelle statiche
 * ──────────────────────────────────────────────────────────────────────────── */

/*
 * Samplerate LA — 12 valori da 500MHz a 100kHz.
 * Formato: (period_num, period_den) in secondi.
 * SPERIOD = period_num / period_den
 * Esempio: (2, 1000000000) → 2ns → 500MHz
 */
const uint64_t hp1660es_la_samplerates[HP1660ES_NUM_LA_SAMPLERATES][2] = {
	{          2, 1000000000 },  /*  0: 2ns   → 500MHz */
	{          4, 1000000000 },  /*  1: 4ns   → 250MHz */
	{         10, 1000000000 },  /*  2: 10ns  → 100MHz */
	{         20, 1000000000 },  /*  3: 20ns  →  50MHz */
	{         40, 1000000000 },  /*  4: 40ns  →  25MHz */
	{        100, 1000000000 },  /*  5: 100ns →  10MHz */
	{        200, 1000000000 },  /*  6: 200ns →   5MHz */
	{        500, 1000000000 },  /*  7: 500ns →   2MHz */
	{       1000, 1000000000 },  /*  8: 1µs   →   1MHz */
	{       2000, 1000000000 },  /*  9: 2µs   → 500kHz */
	{       5000, 1000000000 },  /* 10: 5µs   → 200kHz */
	{      10000, 1000000000 },  /* 11: 10µs  → 100kHz */
};

/*
 * Timebases OSC — 31 valori.
 * Formato: (num, den) → T/div = num/den secondi.
 * Il 1660ES riporta TIMebase:RANGe come range TOTALE (10 div).
 * T/div = RANGe / 10.
 */
const uint64_t hp1660es_osc_timebases[HP1660ES_NUM_OSC_TIMEBASES][2] = {
	/* nanosecondi/div */
	{   1, 1000000000 },  /*  0:  1ns/div */
	{   2, 1000000000 },  /*  1:  2ns/div */
	{   5, 1000000000 },  /*  2:  5ns/div */
	{  10, 1000000000 },  /*  3: 10ns/div */
	{  20, 1000000000 },  /*  4: 20ns/div */
	{  50, 1000000000 },  /*  5: 50ns/div */
	{ 100, 1000000000 },  /*  6: 100ns/div */
	{ 200, 1000000000 },  /*  7: 200ns/div */
	{ 500, 1000000000 },  /*  8: 500ns/div */
	/* microsecondi/div */
	{   1, 1000000 },     /*  9:   1µs/div */
	{   2, 1000000 },     /* 10:   2µs/div */
	{   5, 1000000 },     /* 11:   5µs/div */
	{  10, 1000000 },     /* 12:  10µs/div */
	{  20, 1000000 },     /* 13:  20µs/div */
	{  50, 1000000 },     /* 14:  50µs/div */
	{ 100, 1000000 },     /* 15: 100µs/div */
	{ 200, 1000000 },     /* 16: 200µs/div */
	{ 500, 1000000 },     /* 17: 500µs/div */
	/* millisecondi/div */
	{   1, 1000 },        /* 18:   1ms/div  ← default (segnale ESR70) */
	{   2, 1000 },        /* 19:   2ms/div */
	{   5, 1000 },        /* 20:   5ms/div */
	{  10, 1000 },        /* 21:  10ms/div */
	{  20, 1000 },        /* 22:  20ms/div */
	{  50, 1000 },        /* 23:  50ms/div */
	{ 100, 1000 },        /* 24: 100ms/div */
	{ 200, 1000 },        /* 25: 200ms/div */
	{ 500, 1000 },        /* 26: 500ms/div */
	/* secondi/div */
	{  1, 1 },            /* 27:   1s/div */
	{  2, 1 },            /* 28:   2s/div */
	{  5, 1 },            /* 29:   5s/div */
	{ 10, 1 },            /* 30:  10s/div */
};

/*
 * V/div OSC — 14 valori.
 * Formato: (num_mV, den) → V/div = num_mV/den
 * Il 1660ES riporta CHANnel:RANGe come range TOTALE su 4 divisioni.
 * V/div = RANGe / 4.  (dal manuale: "RANGe = 4 × Volts/Div")
 */
const uint64_t hp1660es_osc_vdivs[HP1660ES_NUM_OSC_VDIVS][2] = {
	{   2, 1000 },  /*  0:   2mV/div */
	{   5, 1000 },  /*  1:   5mV/div */
	{  10, 1000 },  /*  2:  10mV/div */
	{  20, 1000 },  /*  3:  20mV/div */
	{  50, 1000 },  /*  4:  50mV/div */
	{ 100, 1000 },  /*  5: 100mV/div */
	{ 200, 1000 },  /*  6: 200mV/div */
	{ 500, 1000 },  /*  7: 500mV/div */
	{   1, 1 },     /*  8:   1V/div  */
	{   2, 1 },     /*  9:   2V/div  */
	{   5, 1 },     /* 10:   5V/div  */
	{  10, 1 },     /* 11:  10V/div  */
	{  20, 1 },     /* 12:  20V/div  */
	{  50, 1 },     /* 13:  50V/div  */
};

/* Nomi canali digitali — 128 stringhe "A1.0".."A8.15"
 *
 * Mapping pod fisici → indici globali:
 *   POD_A1 (A1.0..A1.15)  → indici   0..15   clock J
 *   POD_A2 (A2.0..A2.15)  → indici  16..31   clock K
 *   POD_A3 (A3.0..A3.15)  → indici  32..47   clock L
 *   POD_A4 (A4.0..A4.15)  → indici  48..63   clock M
 *   POD_A5 (A5.0..A5.15)  → indici  64..79   clock ? (da verificare)
 *   POD_A6 (A6.0..A6.15)  → indici  80..95   clock ? (da verificare)
 *   POD_A7 (A7.0..A7.15)  → indici  96..111  clock N
 *   POD_A8 (A8.0..A8.15)  → indici 112..127  clock P
 */
static const char *digital_channel_names[HP1660ES_N_DIG_CHANNELS] = {
	/* POD_A1 (indici 0..15) */
	"A1.0", "A1.1", "A1.2", "A1.3", "A1.4", "A1.5", "A1.6", "A1.7",
	"A1.8", "A1.9", "A1.10","A1.11","A1.12","A1.13","A1.14","A1.15",
	/* POD_A2 (indici 16..31) */
	"A2.0", "A2.1", "A2.2", "A2.3", "A2.4", "A2.5", "A2.6", "A2.7",
	"A2.8", "A2.9", "A2.10","A2.11","A2.12","A2.13","A2.14","A2.15",
	/* POD_A3 (indici 32..47) */
	"A3.0", "A3.1", "A3.2", "A3.3", "A3.4", "A3.5", "A3.6", "A3.7",
	"A3.8", "A3.9", "A3.10","A3.11","A3.12","A3.13","A3.14","A3.15",
	/* POD_A4 (indici 48..63) */
	"A4.0", "A4.1", "A4.2", "A4.3", "A4.4", "A4.5", "A4.6", "A4.7",
	"A4.8", "A4.9", "A4.10","A4.11","A4.12","A4.13","A4.14","A4.15",
	/* POD_A5 (indici 64..79) */
	"A5.0", "A5.1", "A5.2", "A5.3", "A5.4", "A5.5", "A5.6", "A5.7",
	"A5.8", "A5.9", "A5.10","A5.11","A5.12","A5.13","A5.14","A5.15",
	/* POD_A6 (indici 80..95) */
	"A6.0", "A6.1", "A6.2", "A6.3", "A6.4", "A6.5", "A6.6", "A6.7",
	"A6.8", "A6.9", "A6.10","A6.11","A6.12","A6.13","A6.14","A6.15",
	/* POD_A7 (indici 96..111) */
	"A7.0", "A7.1", "A7.2", "A7.3", "A7.4", "A7.5", "A7.6", "A7.7",
	"A7.8", "A7.9", "A7.10","A7.11","A7.12","A7.13","A7.14","A7.15",
	/* POD_A8 (indici 112..127) */
	"A8.0", "A8.1", "A8.2", "A8.3", "A8.4", "A8.5", "A8.6", "A8.7",
	"A8.8", "A8.9", "A8.10","A8.11","A8.12","A8.13","A8.14","A8.15",
};

static const char *pod_names[HP1660ES_N_PODS] = {
	"POD_A1", "POD_A2", "POD_A3", "POD_A4",
	"POD_A5", "POD_A6", "POD_A7", "POD_A8",
};

static const char *analog_channel_names[HP1660ES_N_ANALOG_CHANNELS] = {
	"OSC1", "OSC2",
};

static const char *coupling_options[] = {
	"AC", "DC", "DC50",
};

/*
 * Sorgenti trigger OSC.
 * Quirk: lo strumento risponde "CHANNEL1" (maiuscolo) ma accetta "CHANnel1".
 * Usiamo la forma abbreviata SCPI nei comandi SET.
 */
static const char *trigger_sources[] = {
	"CHANnel1", "CHANnel2", "EXTernal", "LINE",
};

/*
 * Configurazione statica dell'unico modello supportato.
 * Inizializzata in hp1660es_device_init() con i dati dall'IDN.
 */
static struct scope_config hp1660es_config = {
	.model_id            = "1660E",      /* aggiornato da IDN in device_init */
	.model_name          = "HP 1660ES",  /* aggiornato da model_match */
	.firmware_ver        = "",           /* aggiornato da IDN */

	.digital_channels    = HP1660ES_N_DIG_CHANNELS,
	.analog_channels     = HP1660ES_N_ANALOG_CHANNELS,
	.pods                = HP1660ES_N_PODS,

	.digital_names       = &digital_channel_names,
	.pod_names           = &pod_names,
	.analog_names        = &analog_channel_names,

	.coupling_options    = &coupling_options,
	.num_coupling_options = ARRAY_SIZE(coupling_options),

	.trigger_sources     = &trigger_sources,
	.num_trigger_sources = ARRAY_SIZE(trigger_sources),

	.num_xdivs           = 10,
	.num_ydivs           = 4,
};

/* ─────────────────────────────────────────────────────────────────────────────
 * Model matching
 *
 * IDN: "HEWLETT PACKARD,1660E,0,REV 02.02"
 * Campo model = "1660E" — il suffisso 'S' non è presente.
 * Usiamo prefix matching bidirezionale: "1660E" matcha "1660ES" e viceversa.
 * ──────────────────────────────────────────────────────────────────────────── */

static const char * const hp166x_model_ids[] = {
	"1660A", "1660C", "1660E", "1660ES",
	"1661A", "1661C", "1661E", "1661ES",
	NULL
};

static const char * const hp166x_model_names[] = {
	"HP 1660A", "HP 1660C", "HP 1660E", "HP 1660ES",
	"HP 1661A", "HP 1661C", "HP 1661E", "HP 1661ES",
	NULL
};

SR_PRIV int hp1660es_model_match(const char *idn_model,
		char *model_name_out, size_t name_len)
{
	int i;
	const char *id;

	if (!idn_model || !model_name_out)
		return SR_ERR_ARG;

	for (i = 0; (id = hp166x_model_ids[i]); i++) {
		/*
		 * Prefix matching bidirezionale:
		 * "1660E" matcha "1660ES" (IDN senza suffisso S)
		 * "1660ES" matcha "1660E" (per sicurezza)
		 */
		if (g_str_has_prefix(idn_model, id) ||
		    g_str_has_prefix(id, idn_model)) {
			g_strlcpy(model_name_out, hp166x_model_names[i], name_len);
			return SR_OK;
		}
	}

	sr_err("Unsupported HP 166x model: %s", idn_model);
	return SR_ERR_NA;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * scope_state alloc / free
 * ──────────────────────────────────────────────────────────────────────────── */

static struct scope_state *hp1660es_scope_state_new(void)
{
	struct scope_state *state;

	state = g_malloc0(sizeof(struct scope_state));

	state->analog_states = g_malloc0(HP1660ES_N_ANALOG_CHANNELS *
			sizeof(struct analog_channel_state));

	state->digital_states = g_malloc0(HP1660ES_N_DIG_CHANNELS *
			sizeof(gboolean));

	state->pod_states = g_malloc0(HP1660ES_N_PODS *
			sizeof(struct pod_state));

	/* Valori default */
	state->osc_timebase_idx  = 18;       /* 1ms/div */
	state->osc_acq_count     = 1;
	state->osc_horiz_triggerpos = 0.5f;
	g_strlcpy(state->la_machine_type,      "TIMING",             sizeof(state->la_machine_type));
	g_strlcpy(state->la_acqmode,           "CONVENTIONAL, FULL", sizeof(state->la_acqmode));
	g_strlcpy(state->la_state_clock_id,    "J",                  sizeof(state->la_state_clock_id));
	g_strlcpy(state->la_state_clock_spec,  "RISing",             sizeof(state->la_state_clock_spec));
	state->la_state_sethold = 0;   /* 3.5ns setup / 0.0ns hold — tabella 15-2 */
	g_strlcpy(state->la_arm_source,        "RUN",                sizeof(state->la_arm_source));
	g_strlcpy(state->osc_acq_type,         "NORMal",             sizeof(state->osc_acq_type));
	g_strlcpy(state->osc_timebase_mode, "AUTO",          sizeof(state->osc_timebase_mode));
	g_strlcpy(state->osc_trigger_source, "CHANnel1",     sizeof(state->osc_trigger_source));
	g_strlcpy(state->osc_trigger_slope,  "POSitive",     sizeof(state->osc_trigger_slope));

	/* Threshold default TTL per tutti i pod */
	for (int i = 0; i < HP1660ES_N_PODS; i++) {
		state->pod_states[i].threshold_v     = 1.5f;
		state->pod_states[i].threshold_idx   = 0;  /* TTL */
		state->pod_states[i].threshold_preset = TRUE;
	}

	/* Default analog states */
	for (int i = 0; i < HP1660ES_N_ANALOG_CHANNELS; i++) {
		state->analog_states[i].probe_factor   = 1;
		state->analog_states[i].yreference     = 64.0f;
		state->analog_states[i].num_samples    = 32768;
	}

	return state;
}

SR_PRIV void hp1660es_scope_state_destroy(struct scope_state *state)
{
	if (!state)
		return;
	g_free(state->analog_states);
	g_free(state->digital_states);
	g_free(state->pod_states);
	g_free(state);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * device_init
 * Equivalente a dlm_device_init() — costruisce channel groups e registra
 * canali nell'sr_dev_inst.
 * ──────────────────────────────────────────────────────────────────────────── */

SR_PRIV int hp1660es_device_init(struct sr_dev_inst *sdi)
{
	int i, j;
	struct sr_channel *ch;
	struct dev_context *devc;

	devc = sdi->priv;

	/* Alloca array di channel group */
	devc->analog_groups  = g_malloc0(HP1660ES_N_ANALOG_CHANNELS *
			sizeof(struct sr_channel_group *));
	devc->digital_groups = g_malloc0(HP1660ES_N_PODS *
			sizeof(struct sr_channel_group *));

	if (!devc->analog_groups || !devc->digital_groups) {
		g_free(devc->analog_groups);
		g_free(devc->digital_groups);
		return SR_ERR_MALLOC;
	}

	/*
	 * Canali analogici OSC — indici 128..129.
	 * Ciascuno nel proprio channel group (OSC1, OSC2).
	 */
	for (i = 0; i < HP1660ES_N_ANALOG_CHANNELS; i++) {
		ch = sr_channel_new(sdi,
				HP1660ES_ANALOG_CHAN_INDEX_OFFS + i,
				SR_CHANNEL_ANALOG, TRUE,
				analog_channel_names[i]);

		devc->analog_groups[i] = sr_channel_group_new(sdi,
				analog_channel_names[i], NULL);
		devc->analog_groups[i]->channels =
				g_slist_append(NULL, ch);
	}

	/*
	 * Channel groups digitali LA — limitati a HP1660ES_PULSEVIEW_MAX_PODS.
	 *
	 * NOTA: PulseView (versione git su Arch Linux, marzo 2026) crasha con
	 * throw QString quando il file .sr contiene più di 64 canali logici.
	 * Il crash è causato da output/srzip di libsigrok che scrive
	 * total probes basandosi su TUTTI i canali registrati nel device
	 * (indipendentemente da ch->enabled), producendo un mismatch tra
	 * total probes e unitsize che PulseView non gestisce.
	 *
	 * Soluzione temporanea: registriamo solo i primi 4 pod (64 canali)
	 * invece di tutti gli 8 (128 canali). I pod A5..A8 non sono
	 * accessibili finché questo limite non viene rimosso.
	 *
	 * TODO: quando output/srzip rispetterà ch->enabled nel conteggio
	 * di total probes, riportare HP1660ES_PULSEVIEW_MAX_PODS a
	 * HP1660ES_N_PODS (8) per abilitare tutti i 128 canali.
	 */
#define HP1660ES_PULSEVIEW_MAX_PODS  4   /* 4 pod × 16 bit = 64 canali */

	for (i = 0; i < HP1660ES_PULSEVIEW_MAX_PODS; i++) {
		devc->digital_groups[i] = sr_channel_group_new(sdi,
				pod_names[i], NULL);
		if (!devc->digital_groups[i])
			return SR_ERR_MALLOC;
	}

	/*
	 * Canali digitali LA — indici 0..(MAX_PODS×16 - 1).
	 * I punti nel nome (es. "A1.0") vengono preservati qui;
	 * write_vcd() li converte in underscore per compatibilità VCD.
	 */
	for (i = 0; i < HP1660ES_PULSEVIEW_MAX_PODS * HP1660ES_BITS_PER_POD; i++) {
		ch = sr_channel_new(sdi,
				HP1660ES_DIG_CHAN_INDEX_OFFS + i,
				SR_CHANNEL_LOGIC, TRUE,
				digital_channel_names[i]);

		j = i / HP1660ES_BITS_PER_POD;  /* pod index 0..3 */
		devc->digital_groups[j]->channels =
				g_slist_append(devc->digital_groups[j]->channels, ch);
	}

	devc->model_config = &hp1660es_config;
	devc->frame_limit  = 0;

	if (!(devc->model_state = hp1660es_scope_state_new()))
		return SR_ERR_MALLOC;

	return SR_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * SCPI helpers locali
 * ──────────────────────────────────────────────────────────────────────────── */

/*
 * Invia un comando SCPI e aspetta *OPC? per conferma.
 * Usato per comandi SET che devono completarsi prima del prossimo.
 */
static int scpi_cmd(struct sr_scpi_dev_inst *scpi, const char *cmd)
{
	if (sr_scpi_send(scpi, cmd) != SR_OK)
		return SR_ERR;
	return SR_OK;
}

/*
 * Svuota la coda errori SCPI in loop fino a risposta "0" o "+0".
 * Quirk HP 1660ES: *CLS azzera ESR ma NON svuota SYSTEM:ERROR? —
 * serve leggere in loop.
 * Ritorna SR_OK se nessun errore fatale, SR_ERR se errori non-zero.
 */
SR_PRIV int hp1660es_drain_errors(struct sr_scpi_dev_inst *scpi)
{
	char *resp;
	int i, has_error;

	has_error = 0;

	for (i = 0; i < 16; i++) {
		if (sr_scpi_get_string(scpi, ":SYSTEM:ERROR?", &resp) != SR_OK)
			break;

		if (!resp)
			break;

		if (strcmp(resp, "0") == 0 || strcmp(resp, "+0") == 0) {
			g_free(resp);
			break;
		}

		sr_warn("HP1660ES SCPI error: %s", resp);
		has_error = 1;
		g_free(resp);
	}

	return has_error ? SR_ERR : SR_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Parsing float da stringa SCPI (es. "+4.00000E-09")
 * Versione semplificata rispetto a array_float_get() del DLM —
 * per i nostri usi g_ascii_strtod() è sufficiente.
 * ──────────────────────────────────────────────────────────────────────────── */

static int parse_float(const char *str, float *out)
{
	char *end;
	double val;

	if (!str || !*str)
		return SR_ERR;

	val = g_ascii_strtod(str, &end);
	if (end == str)
		return SR_ERR;

	*out = (float)val;
	return SR_OK;
}

/*
 * Cerca l'indice più vicino in una tabella (num, den) per un valore float.
 * Usato per mappare RANGe → vdiv_idx e TIMebase:RANGe → timebase_idx.
 */
static int nearest_table_idx(float value, const uint64_t table[][2],
		int n, float divisor)
{
	int i, best_idx;
	float best_err, err, entry_val;

	best_idx = 0;
	best_err = INFINITY;

	for (i = 0; i < n; i++) {
		entry_val = (float)table[i][0] / (float)table[i][1];
		err = fabsf(entry_val - value / divisor);
		if (err < best_err) {
			best_err = err;
			best_idx = i;
		}
	}

	return best_idx;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * scope_state_query_la — legge stato modulo LA
 *
 * Quirk HP 1660ES verificati su hardware (REV 02.02):
 *   - :SELECT 1 obbligatorio prima di :MACHINE1:*
 *   - :RMODE? genera errore -132 dopo SELECT 1 → NON interrogare
 *   - ACQMODE risponde "CONVENTIONAL,FULL" senza spazio → normalizzare
 *   - THRESHOLD1..8 (uno per pod A1..A8) — verificato su hardware
 *   - THRESHOLD9+ → out of range sullo schermo
 * ──────────────────────────────────────────────────────────────────────────── */

static int scope_state_query_la(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct scope_state *state;
	struct sr_scpi_dev_inst *scpi;
	char *resp, cmd[64];
	float period_s;
	int i;

	devc  = sdi->priv;
	state = devc->model_state;
	scpi  = sdi->conn;

	sr_info("HP1660ES: reading LA state");

	/* Machine type */
	if (sr_scpi_get_string(scpi, ":MACHINE1:TYPE?", &resp) == SR_OK && resp) {
		g_strlcpy(state->la_machine_type, resp,
				sizeof(state->la_machine_type));
		g_free(resp);
	}

	/* Se siamo in STATE mode, leggi il clock master corrente.
	 * Query: :MACHINE1:SFORMAT:MASTER? <clock_id>
	 * La risposta non ha un formato query diretto per "tutti i clock" —
	 * il manuale mostra solo SLAVE? <clock_id>. Per MASTER usiamo
	 * la query del pod 1 come riferimento (il pod con indice basso
	 * della prima coppia attiva usa sempre il master clock).
	 * Nota: SFORMAT:MASTER? non esiste come query standalone sul 1660E;
	 * usiamo SFORMAT:CLOCK1? che risponde MASTer|SLAVe|DEMultiplex.
	 * Il clock_id (J/K/...) non è leggibile via query — lo manteniamo
	 * al valore configurato dall'utente (default "J"). */
	if (g_ascii_strcasecmp(state->la_machine_type, "STATE") == 0) {
		sr_info("HP1660ES: STATE mode detected — clock %s,%s sethold=%d",
			state->la_state_clock_id,
			state->la_state_clock_spec,
			state->la_state_sethold);
		/* Non sovrascriviamo clock_id/spec dall'hardware perché
		 * il 1660E non espone query per leggere il clock_id corrente.
		 * I valori restano quelli impostati dall'utente o il default J,RISing. */
	}

	/* ACQMODE — normalizza virgola+spazio */
	if (sr_scpi_get_string(scpi, ":MACHINE1:TFORMAT:ACQMODE?", &resp) == SR_OK && resp) {
		/* "CONVENTIONAL,FULL" → "CONVENTIONAL, FULL" */
		GString *gs = g_string_new(resp);
		g_free(resp);
		gchar **parts = g_strsplit(gs->str, ",", 2);
		if (parts[0] && parts[1]) {
			g_snprintf(state->la_acqmode, sizeof(state->la_acqmode),
					"%s, %s", g_strstrip(parts[0]), g_strstrip(parts[1]));
		}
		g_strfreev(parts);
		g_string_free(gs, TRUE);
	}

	/* SPERIOD — sample period → samplerate */
	if (sr_scpi_get_string(scpi, ":MACHINE1:TTRIGGER:SPERIOD?", &resp) == SR_OK && resp) {
		if (parse_float(resp, &period_s) == SR_OK && period_s > 0) {
			state->la_sample_period_ps = (uint64_t)(period_s * 1e12);
			state->la_samplerate_hz    = (uint64_t)(1.0 / period_s + 0.5);

			/* Cerca indice nella tabella samplerate */
			state->la_samplerate_idx = -1;
			for (i = 0; i < HP1660ES_NUM_LA_SAMPLERATES; i++) {
				float tbl_period = (float)hp1660es_la_samplerates[i][0] /
						(float)hp1660es_la_samplerates[i][1];
				if (fabsf(tbl_period - period_s) / period_s < 0.01f) {
					state->la_samplerate_idx = i;
					break;
				}
			}
		}
		g_free(resp);
		sr_info("HP1660ES LA: samplerate=%" PRIu64 " Hz  period=%" PRIu64 " ps",
				state->la_samplerate_hz, state->la_sample_period_ps);
	}

	/*
	 * THRESHOLD1..8 — uno per pod A1..A8.
	 * Verificato su hardware REV 02.02: esistono THRESHOLD1..8.
	 * THRESHOLD9+ risponde "out of range" sullo schermo.
	 * Preset riconosciuti: TTL=1.5V, ECL=-1.3V, CMOS=2.5V.
	 */
	for (i = 0; i < HP1660ES_N_PODS; i++) {
		g_snprintf(cmd, sizeof(cmd), ":MACHINE1:TFORMAT:THRESHOLD%d?", i + 1);
		if (sr_scpi_get_string(scpi, cmd, &resp) != SR_OK || !resp)
			continue;

		if (strcmp(resp, "TTL") == 0) {
			state->pod_states[i].threshold_v      = 1.5f;
			state->pod_states[i].threshold_idx    = 0;
			state->pod_states[i].threshold_preset = TRUE;
		} else if (strcmp(resp, "ECL") == 0) {
			state->pod_states[i].threshold_v      = -1.3f;
			state->pod_states[i].threshold_idx    = 1;
			state->pod_states[i].threshold_preset = TRUE;
		} else if (strcmp(resp, "CMOS") == 0) {
			state->pod_states[i].threshold_v      = 2.5f;
			state->pod_states[i].threshold_idx    = 2;
			state->pod_states[i].threshold_preset = TRUE;
		} else {
			float thr;
			if (parse_float(resp, &thr) == SR_OK) {
				state->pod_states[i].threshold_v      = thr;
				state->pod_states[i].threshold_idx    = -1;
				state->pod_states[i].threshold_preset = FALSE;
			}
		}
		g_free(resp);

		sr_info("HP1660ES LA: threshold%d (pod A%d): %.2fV preset=%d",
				i + 1, i + 1,
				state->pod_states[i].threshold_v,
				state->pod_states[i].threshold_preset);
	}

	/* Trigger sequence e find1 (informativi) */
	if (sr_scpi_get_string(scpi, ":MACHINE1:TTRIGGER:SEQUENCE?", &resp) == SR_OK && resp) {
		sr_dbg("HP1660ES LA: trigger sequence: %s", resp);
		g_free(resp);
	}

	/*
	 * Quirk: :RMODE? genera errore -132 dopo :SELECT 1 → NON interrogare.
	 * Drain errori non fatali prima di procedere.
	 */
	hp1660es_drain_errors(scpi);

	return SR_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * scope_state_query_osc — legge stato modulo OSC
 *
 * Quirk HP 1660ES verificati su hardware (REV 02.02):
 *   - :SELECT 2 obbligatorio prima delle query OSC
 *   - RANGe = range TOTALE in V su 4 divisioni → V/div = RANGe / 4
 *     (manuale: ":CHANnel:RANGe defines the full-scale (4 × Volts/Div)")
 *   - TIMebase:RANGe = range TOTALE in s → T/div = RANGe / 10
 *   - TIMebase:MODE? = "AUTO" read-only (SET → errore -100)
 *   - ACQuire:COUNt SET valido solo se ACQuire:TYPE AVERage già impostato
 *   - TRIGger:SOURce risponde "CHANNEL1" → normalizzare a "CHANnel1"
 *   - PROBe: valori pratici solo 1 o 10
 * ──────────────────────────────────────────────────────────────────────────── */

static int scope_state_query_osc(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct scope_state *state;
	struct sr_scpi_dev_inst *scpi;
	const struct scope_config *config;
	char *resp, cmd[64];
	float fval;
	int i;

	devc   = sdi->priv;
	state  = devc->model_state;
	scpi   = sdi->conn;
	config = devc->model_config;

	sr_info("HP1660ES: reading OSC state");

	/* Canali analogici */
	for (i = 0; i < HP1660ES_N_ANALOG_CHANNELS; i++) {
		struct analog_channel_state *ach = &state->analog_states[i];
		int ch_n = i + 1;

		/* RANGe — range totale V */
		g_snprintf(cmd, sizeof(cmd), ":CHANnel%d:RANGe?", ch_n);
		if (sr_scpi_get_string(scpi, cmd, &resp) == SR_OK && resp) {
			if (parse_float(resp, &fval) == SR_OK) {
				ach->waveform_range = fval;
				ach->vdiv_idx = nearest_table_idx(fval,
						hp1660es_osc_vdivs,
						HP1660ES_NUM_OSC_VDIVS,
						(float)config->num_ydivs);
				sr_dbg("HP1660ES OSC query CH%d: "
				       "RANGe_resp=%.6fV num_ydivs=%d "
				       "-> vdiv=%.4fV/div (idx=%d)",
				       ch_n, fval, config->num_ydivs,
				       (float)hp1660es_osc_vdivs[ach->vdiv_idx][0] /
				       (float)hp1660es_osc_vdivs[ach->vdiv_idx][1],
				       ach->vdiv_idx);
			}
			g_free(resp);
		}

		/* OFFSet */
		g_snprintf(cmd, sizeof(cmd), ":CHANnel%d:OFFSet?", ch_n);
		if (sr_scpi_get_string(scpi, cmd, &resp) == SR_OK && resp) {
			if (parse_float(resp, &fval) == SR_OK) {
				ach->vertical_offset = fval;
				ach->waveform_offset = fval;
			}
			g_free(resp);
		}

		/* COUPling */
		g_snprintf(cmd, sizeof(cmd), ":CHANnel%d:COUPling?", ch_n);
		if (sr_scpi_get_string(scpi, cmd, &resp) == SR_OK && resp) {
			/* Trova indice in coupling_options[] */
			ach->coupling = 1;  /* default DC */
			for (int k = 0; k < (int)ARRAY_SIZE(coupling_options); k++) {
				if (g_ascii_strcasecmp(resp, coupling_options[k]) == 0) {
					ach->coupling = k;
					break;
				}
			}
			g_free(resp);
		}

		/* PROBe — 1 o 10 */
		g_snprintf(cmd, sizeof(cmd), ":CHANnel%d:PROBe?", ch_n);
		if (sr_scpi_get_string(scpi, cmd, &resp) == SR_OK && resp) {
			if (parse_float(resp, &fval) == SR_OK)
				ach->probe_factor = (int)(fval + 0.5f);
			g_free(resp);
		}

		sr_info("HP1660ES OSC: CH%d range=%.3fV vdiv_idx=%d "
				"offset=%.3fV coupling=%d probe=%dx",
				ch_n, ach->waveform_range, ach->vdiv_idx,
				ach->vertical_offset, ach->coupling, ach->probe_factor);
	}

	/* TIMebase:RANGe — range totale s */
	if (sr_scpi_get_string(scpi, ":TIMebase:RANGe?", &resp) == SR_OK && resp) {
		if (parse_float(resp, &fval) == SR_OK) {
			state->osc_timebase_idx = nearest_table_idx(fval,
					hp1660es_osc_timebases,
					HP1660ES_NUM_OSC_TIMEBASES,
					(float)config->num_xdivs);
		}
		g_free(resp);
	}

	/* TIMebase:DELay — SET ok, float in secondi */
	if (sr_scpi_get_string(scpi, ":TIMebase:DELay?", &resp) == SR_OK && resp) {
		if (parse_float(resp, &fval) == SR_OK)
			state->osc_timebase_delay_s = fval;
		g_free(resp);
	}

	/* TIMebase:MODE — read-only, risponde sempre "AUTO" */
	if (sr_scpi_get_string(scpi, ":TIMebase:MODE?", &resp) == SR_OK && resp) {
		g_strlcpy(state->osc_timebase_mode, resp,
				sizeof(state->osc_timebase_mode));
		g_free(resp);
	}

	/* ACQuire:TYPE */
	if (sr_scpi_get_string(scpi, ":ACQuire:TYPE?", &resp) == SR_OK && resp) {
		g_strlcpy(state->osc_acq_type, resp, sizeof(state->osc_acq_type));
		g_free(resp);
	}

	/*
	 * ACQuire:COUNt — read safe, ma SET valido solo se TYPE=AVERage.
	 * Leggiamo comunque per aggiornare lo state.
	 */
	if (sr_scpi_get_string(scpi, ":ACQuire:COUNt?", &resp) == SR_OK && resp) {
		if (parse_float(resp, &fval) == SR_OK)
			state->osc_acq_count = (int)(fval + 0.5f);
		g_free(resp);
	}

	/* TRIGger:SOURce — risponde "CHANNEL1", normalizzare a "CHANnel1" */
	if (sr_scpi_get_string(scpi, ":TRIGger:SOURce?", &resp) == SR_OK && resp) {
		/* Normalizza CHANNEL1 → CHANnel1 */
		if (g_str_has_prefix(resp, "CHANNEL")) {
			g_snprintf(state->osc_trigger_source,
					sizeof(state->osc_trigger_source),
					"CHANnel%s", resp + 7);
		} else {
			g_strlcpy(state->osc_trigger_source, resp,
					sizeof(state->osc_trigger_source));
		}
		g_free(resp);
	}

	/* TRIGger:SLOPe */
	if (sr_scpi_get_string(scpi, ":TRIGger:SLOPe?", &resp) == SR_OK && resp) {
		g_strlcpy(state->osc_trigger_slope, resp,
				sizeof(state->osc_trigger_slope));
		g_free(resp);
	}

	/* TRIGger:LEVel */
	if (sr_scpi_get_string(scpi, ":TRIGger:LEVel?", &resp) == SR_OK && resp) {
		parse_float(resp, &state->osc_trigger_level_v);
		g_free(resp);
	}

	sr_info("HP1660ES OSC: timebase_idx=%d delay=%.6fs trigger=%s %s %.3fV",
			state->osc_timebase_idx,
			state->osc_timebase_delay_s,
			state->osc_trigger_source,
			state->osc_trigger_slope,
			state->osc_trigger_level_v);

	hp1660es_drain_errors(scpi);

	return SR_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * hp1660es_scope_state_query — entry point principale
 *
 * Sequenza:
 *   1. *CLS — pulisce error queue
 *   2. :SELECT 1 → legge stato LA
 *   3. :SELECT 2 → legge stato OSC
 *
 * Nota: *CLS deve precedere :SELECT per evitare errore -212 (Init ignored)
 * che appare se la macchina è rimasta in stato sporco da sessioni precedenti.
 * ──────────────────────────────────────────────────────────────────────────── */

SR_PRIV int hp1660es_scope_state_query(struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi;
	int ret;

	scpi = sdi->conn;

	/* *CLS — deve venire PRIMA di SELECT per evitare -212 */
	sr_scpi_send(scpi, "*CLS");
	g_usleep(300000);  /* 300ms */

	/* Seleziona modulo LA */
	if (scpi_cmd(scpi, ":SELECT 1") != SR_OK)
		return SR_ERR;
	g_usleep(200000);
	hp1660es_drain_errors(scpi);

	ret = scope_state_query_la(sdi);
	if (ret != SR_OK)
		return SR_ERR;

	/* Seleziona modulo OSC */
	if (scpi_cmd(scpi, ":SELECT 2") != SR_OK)
		return SR_ERR;
	g_usleep(200000);
	hp1660es_drain_errors(scpi);

	ret = scope_state_query_osc(sdi);
	if (ret != SR_OK)
		return SR_ERR;

	{
		struct dev_context *devc = sdi->priv;
		hp1660es_scope_state_dump(devc->model_config, devc->model_state);
	}

	return SR_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * check_channels
 *
 * HP 1660ES: LA e OSC sono moduli hardware separati.
 * Non è possibile acquisire entrambi nello stesso trigger.
 * Configurazione mista → SR_ERR_NA.
 * ──────────────────────────────────────────────────────────────────────────── */

SR_PRIV int hp1660es_check_channels(GSList *channels)
{
	GSList *l;
	struct sr_channel *ch;
	gboolean has_logic, has_analog;

	has_logic = has_analog = FALSE;

	for (l = channels; l; l = l->next) {
		ch = l->data;
		switch (ch->type) {
		case SR_CHANNEL_ANALOG:
			has_analog = TRUE;
			break;
		case SR_CHANNEL_LOGIC:
			has_logic = TRUE;
			break;
		default:
			return SR_ERR;
		}
	}

	if (has_logic && has_analog) {
		sr_info("HP1660ES: mixed LA+OSC acquisition — "
			"cross-arming via INTermodule (LA→OSC, TIMING only)");
	}

	return SR_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * hp1660es_la_configure — sequenza SCPI per acquisizione LA
 *
 * ── TIMING mode (default) ────────────────────────────────────────────────
 *   :SELECT 1
 *   :MACHINE1:TYPE TIMING                    ← CRITICO: prima di ACQMODE
 *   :MACHINE1:TFORMAT:ACQMODE CONVENTIONAL, FULL
 *   :MACHINE1:TFORMAT:REMOVE ALL             ← pulisce label sessione precedente
 *   :MACHINE1:TFORMAT:LABEL 'Ax',POS,0,<n>,#Bxxxxxxxxxxxxxxxx
 *        ← una per ogni pod con canali abilitati; bitmask indica le linee attive
 *   [SPERIOD viene inviato da la_acquire() — quirk: deve venire dopo LABEL]
 *
 * ── STATE mode (v18) ─────────────────────────────────────────────────────
 *   :SELECT 1
 *   :MACHINE1:TYPE STATE
 *   *CLS                                     ← evita -212 Init ignored
 *   :MACHINE1:SFORMAT:REMOVE ALL             ← reset label precedenti
 *   :MACHINE1:SFORMAT:MASTER <clock_id>,<clock_spec>  ← es. J,RISing
 *   :MACHINE1:SFORMAT:CLOCK<N> MASTER        ← per ogni coppia pod attiva
 *   :MACHINE1:SFORMAT:SETHOLD <pod>,<val>    ← setup/hold per pod attivi
 *   :MACHINE1:ASSIGN <lista pod>             ← assegnazione pod a coppie
 *   :MACHINE1:SFORMAT:LABEL 'Ax',POS,...     ← una label per pod (come TFORMAT)
 *   [niente SPERIOD — clock è esterno al sistema]
 *
 * Configurabile da sigrok-cli:
 *   --config external_clock_source="J,RISing"   ← clock id e edge (STATE mode)
 *   --config capture_ratio=0                     ← setup/hold index 0..9
 * ──────────────────────────────────────────────────────────────────────────── */

SR_PRIV int hp1660es_la_configure(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;
	char cmd[512];
	int i;

	struct scope_state *state;
	devc  = sdi->priv;
	state = devc->model_state;
	scpi  = sdi->conn;

	if (scpi_cmd(scpi, ":SELECT 1") != SR_OK)
		return SR_ERR;
	g_usleep(200000);

	if (g_ascii_strcasecmp(state->la_machine_type, "STATE") == 0) {
		/* ── STATE mode ────────────────────────────────────────────────
		 *
		 * Il campionamento è sincrono al clock del sistema sotto test.
		 * NON si usa TFORMAT (darebbe errore -100 in STATE mode).
		 * Si usa invece il sottosistema SFORMAT.
		 *
		 * Sequenza (dal manuale HP 1660E, capitolo 15 — SFORmat):
		 *   TYPE STATE
		 *   *CLS                               ← evita -212 Init ignored
		 *   SFORMAT:REMOVE ALL                 ← reset label precedenti
		 *   SFORMAT:MASTER <clock_id>,<spec>   ← es. J,RISing
		 *   SFORMAT:CLOCK<N> MASTER            ← per ogni pod coppia attiva
		 *   SFORMAT:SETHOLD <pod_num>,<val>    ← setup/hold per pod attivi
		 *   ASSIGN <lista pod>                 ← assegnazione pod (come TIMING)
		 *   SFORMAT:LABEL ...                  ← una label per pod (come TFORMAT)
		 *
		 * NOTA: niente SPERIOD — il clock è esterno.
		 * NOTA: SFORMAT:CLOCK<N> usa la stessa sintassi <N>={{1|2}|{3|4}...}
		 *       che indicizza coppie di pod, non pod singoli.
		 * ─────────────────────────────────────────────────────────────── */
		sr_info("HP1660ES LA: configuring STATE mode (clock %s,%s sethold=%d)",
			state->la_state_clock_id,
			state->la_state_clock_spec,
			state->la_state_sethold);

		if (scpi_cmd(scpi, ":MACHINE1:TYPE STATE") != SR_OK)
			return SR_ERR;
		g_usleep(100000);
		if (scpi_cmd(scpi, "*CLS") != SR_OK)
			return SR_ERR;
		if (scpi_cmd(scpi, ":MACHINE1:SFORMAT:REMOVE ALL") != SR_OK)
			return SR_ERR;

		/* Master clock: una singola coppia clock_id + clock_spec.
		 * Il manuale dice che clock edges sono ORed — un solo clock
		 * è il caso più comune (bus MSX usa un clock sincrono). */
		g_snprintf(cmd, sizeof(cmd), ":MACHINE1:SFORMAT:MASTER %s,%s",
			state->la_state_clock_id,
			state->la_state_clock_spec);
		sr_info("HP1660ES LA: %s", cmd);
		if (scpi_cmd(scpi, cmd) != SR_OK)
			return SR_ERR;

		/* Pod mask — stessa logica del branch TIMING */
		uint16_t pod_mask_s[HP1660ES_N_PODS] = {0};
		gboolean pod_active_s[HP1660ES_N_PODS] = {0};
		GSList *ls;
		struct sr_channel *chs;

		for (ls = sdi->channels; ls; ls = ls->next) {
			chs = ls->data;
			if (chs->type != SR_CHANNEL_LOGIC || !chs->enabled)
				continue;
			int ch_idx  = chs->index - HP1660ES_DIG_CHAN_INDEX_OFFS;
			int pod_idx = ch_idx / HP1660ES_BITS_PER_POD;
			int bit_idx = ch_idx % HP1660ES_BITS_PER_POD;
			if (pod_idx < 0 || pod_idx >= HP1660ES_N_PODS)
				continue;
			pod_mask_s[pod_idx]  |= (uint16_t)(1u << bit_idx);
			pod_active_s[pod_idx] = TRUE;
		}

		gboolean pair_active_s[HP1660ES_N_PODS / 2] = {0};
		for (i = 0; i < HP1660ES_N_PODS; i++) {
			if (pod_active_s[i])
				pair_active_s[i / 2] = TRUE;
		}

		/* SFORMAT:CLOCK<N> MASTER per ogni coppia di pod attiva.
		 * <N> segue la notazione a coppie: {1|2},{3|4},{5|6},{7|8}
		 * Si usa il numero basso della coppia (1,3,5,7). */
		for (i = 0; i < HP1660ES_N_PODS / 2; i++) {
			if (!pair_active_s[i])
				continue;
			int clock_n = i * 2 + 1;  /* 1,3,5,7 */
			g_snprintf(cmd, sizeof(cmd),
				":MACHINE1:SFORMAT:CLOCK%d MASTER", clock_n);
			sr_info("HP1660ES LA: %s", cmd);
			if (scpi_cmd(scpi, cmd) != SR_OK)
				return SR_ERR;

			/* SETHOLD per pod basso e alto della coppia */
			g_snprintf(cmd, sizeof(cmd),
				":MACHINE1:SFORMAT:SETHOLD %d,%d",
				i * 2 + 1, state->la_state_sethold);
			if (scpi_cmd(scpi, cmd) != SR_OK)
				return SR_ERR;
			g_snprintf(cmd, sizeof(cmd),
				":MACHINE1:SFORMAT:SETHOLD %d,%d",
				i * 2 + 2, state->la_state_sethold);
			if (scpi_cmd(scpi, cmd) != SR_OK)
				return SR_ERR;
		}

		/* ASSIGN — identico al branch TIMING */
		char assign_list_s[64] = {0};
		int first_s = 1;
		int n_active_pairs_s = 0;

		for (i = 0; i < HP1660ES_N_PODS / 2; i++) {
			if (!pair_active_s[i])
				continue;
			n_active_pairs_s++;
			char tmp[16];
			if (!first_s)
				strcat(assign_list_s, ",");
			snprintf(tmp, sizeof(tmp), "%d,%d", i * 2 + 1, i * 2 + 2);
			strcat(assign_list_s, tmp);
			first_s = 0;
		}

		if (n_active_pairs_s == 0) {
			sr_err("HP1660ES LA STATE: no digital channels enabled");
			return SR_ERR;
		}

		g_snprintf(cmd, sizeof(cmd), ":MACHINE1:ASSIGN %s", assign_list_s);
		sr_info("HP1660ES LA: %s", cmd);
		if (scpi_cmd(scpi, cmd) != SR_OK)
			return SR_ERR;

		/* SFORMAT:LABEL — stessa struttura di TFORMAT:LABEL.
		 * Il manuale conferma che SFORMAT:LABel ha la stessa sintassi:
		 *   :MACHine1:SFORmat:LABel <name>,<pol>,<clk>,<upper>,<lower>[,...]
		 * Una label per pod, bitmask dal alto verso il basso. */
		for (i = 0; i < HP1660ES_N_PODS; i++) {
			if (!pod_active_s[i])
				continue;

			char podspec[512];
			char tmp2[64];
			podspec[0] = '\0';

			strcat(podspec, "0");   /* clock bits */

			for (int pair = HP1660ES_N_PODS / 2 - 1; pair >= 0; pair--) {
				if (!pair_active_s[pair])
					continue;
				int pod_low  = pair * 2;
				int pod_high = pair * 2 + 1;
				uint16_t upper = (i == pod_high) ? pod_mask_s[pod_high] : 0;
				uint16_t lower = (i == pod_low)  ? pod_mask_s[pod_low]  : 0;

				char upper_str[17], lower_str[17];
				for (int b = 15; b >= 0; b--) {
					upper_str[15 - b] = (upper >> b) & 1 ? '1' : '0';
					lower_str[15 - b] = (lower >> b) & 1 ? '1' : '0';
				}
				upper_str[16] = '\0';
				lower_str[16] = '\0';

				snprintf(tmp2, sizeof(tmp2), ",#B%s,#B%s",
					upper_str, lower_str);
				strcat(podspec, tmp2);
			}

			g_snprintf(cmd, sizeof(cmd),
				":MACHINE1:SFORMAT:LABEL 'A%d',POS,%s",
				i + 1, podspec);
			sr_info("HP1660ES LA: %s", cmd);
			if (scpi_cmd(scpi, cmd) != SR_OK)
				return SR_ERR;
		}

		/* STATE mode: niente SPERIOD — il clock è quello esterno.
		 * La configurazione è completa: la_acquire() invierà
		 * RMODE SINGLE + START (il polling OPC? funziona uguale). */
		return SR_OK;

	} else {
		/* ── TIMING mode (default) ───────────────────────────────────── */
		if (scpi_cmd(scpi, ":MACHINE1:TYPE TIMING") != SR_OK)
			return SR_ERR;
		if (scpi_cmd(scpi, ":MACHINE1:TFORMAT:REMOVE ALL") != SR_OK)
			return SR_ERR;
		if (scpi_cmd(scpi, ":MACHINE1:TFORMAT:ACQMODE CONVENTIONAL, FULL") != SR_OK)
			return SR_ERR;
	}

	/* ------------------------------------------------------------
	 * Build pod masks
	 * ------------------------------------------------------------ */
	uint16_t pod_mask[HP1660ES_N_PODS] = {0};
	gboolean pod_active[HP1660ES_N_PODS] = {0};
	GSList *l;
	struct sr_channel *ch;

	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->type != SR_CHANNEL_LOGIC || !ch->enabled)
			continue;
		int ch_idx  = ch->index - HP1660ES_DIG_CHAN_INDEX_OFFS;
		int pod_idx = ch_idx / HP1660ES_BITS_PER_POD;
		int bit_idx = ch_idx % HP1660ES_BITS_PER_POD;
		if (pod_idx < 0 || pod_idx >= HP1660ES_N_PODS)
			continue;
		pod_mask[pod_idx]  |= (uint16_t)(1u << bit_idx);
		pod_active[pod_idx] = TRUE;
	}

	/* ------------------------------------------------------------
	 * Find active pairs and build ASSIGN list
	 * ------------------------------------------------------------ */
	gboolean pair_active[HP1660ES_N_PODS / 2] = {0};

	for (i = 0; i < HP1660ES_N_PODS; i++) {
		if (pod_active[i])
			pair_active[i / 2] = TRUE;
	}

	char assign_list[64] = {0};
	int first = 1;
	int n_active_pairs = 0;

	for (i = 0; i < HP1660ES_N_PODS / 2; i++) {
		if (!pair_active[i])
			continue;
		n_active_pairs++;
		int pod_low  = i * 2;
		int pod_high = i * 2 + 1;
		char tmp[16];
		if (!first)
			strcat(assign_list, ",");
		snprintf(tmp, sizeof(tmp), "%d,%d", pod_low + 1, pod_high + 1);
		strcat(assign_list, tmp);
		first = 0;
	}

	if (n_active_pairs == 0) {
		sr_err("HP1660ES LA: no digital channels enabled");
		return SR_ERR;
	}

	snprintf(cmd, sizeof(cmd), ":MACHINE1:ASSIGN %s", assign_list);
	sr_info("HP1660ES LA: %s", cmd);
	if (scpi_cmd(scpi, cmd) != SR_OK)
		return SR_ERR;

	/* ------------------------------------------------------------
	 * Create one label per active pod
	 * ------------------------------------------------------------ */
	for (i = 0; i < HP1660ES_N_PODS; i++) {
		if (!pod_active[i])
			continue;

		char podspec[512];
		char tmp[64];
		podspec[0] = '\0';

		/* clock bits */
		strcat(podspec, "0");

		/* iterate active pairs high → low */
		for (int pair = HP1660ES_N_PODS / 2 - 1; pair >= 0; pair--) {
			if (!pair_active[pair])
				continue;
			int pod_low  = pair * 2;
			int pod_high = pair * 2 + 1;
			uint16_t upper = (i == pod_high) ? pod_mask[pod_high] : 0;
			uint16_t lower = (i == pod_low)  ? pod_mask[pod_low]  : 0;

			char upper_str[17], lower_str[17];
			for (int b = 15; b >= 0; b--) {
				upper_str[15 - b] = (upper >> b) & 1 ? '1' : '0';
				lower_str[15 - b] = (lower >> b) & 1 ? '1' : '0';
			}
			upper_str[16] = '\0';
			lower_str[16] = '\0';

			snprintf(tmp, sizeof(tmp), ",#B%s,#B%s", upper_str, lower_str);
			strcat(podspec, tmp);
		}

		snprintf(cmd, sizeof(cmd),
			":MACHINE1:TFORMAT:LABEL 'A%d',POS,%s",
			i + 1, podspec);
		sr_info("HP1660ES LA: %s", cmd);
		if (scpi_cmd(scpi, cmd) != SR_OK)
			return SR_ERR;
	}

	return SR_OK;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * hp1660es_la_acquire — trigger + download blob
 *
 * Sequenza collaudata:
 *   :RMODE SINGLE
 *   :START
 *   (attesa trigger — timeout configurabile)
 *   :STOP
 *   :SYSTEM:DATA?  → blob IEEE 488.2
 * ──────────────────────────────────────────────────────────────────────────── */

SR_PRIV int hp1660es_la_acquire(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct scope_state *state;
	struct sr_scpi_dev_inst *scpi;
	char *resp;

	devc  = sdi->priv;
	state = devc->model_state;
	scpi  = sdi->conn;

	/*
	 * SPERIOD — solo in TIMING mode.
	 *
	 * In STATE mode il campionamento è sincrono al clock esterno:
	 * non esiste un periodo di campionamento configurabile via SCPI.
	 * Inviare SPERIOD in STATE mode causerebbe un errore silenzioso
	 * (lo strumento ignora o clampla al valore precedente).
	 *
	 * In TIMING mode il quirk v16 rimane invariato:
	 *   - SPERIOD deve essere inviato DOPO le LABEL (già fatto in configure)
	 *   - *OPC? obbligatorio dopo SPERIOD
	 */
	if (g_ascii_strcasecmp(state->la_machine_type, "TIMING") == 0) {
		char cmd[64];
		double period_s = (double)state->la_sample_period_ps / 1e12;
		g_snprintf(cmd, sizeof(cmd),
			":MACHINE1:TTRIGGER:SPERIOD %.6E", period_s);
		sr_info("HP1660ES LA: %s", cmd);
		if (sr_scpi_send(scpi, cmd) != SR_OK)
			return SR_ERR;
		/* *OPC? obbligatorio dopo SPERIOD — quirk v16 */
		if (sr_scpi_get_string(scpi, "*OPC?", &resp) != SR_OK)
			return SR_ERR;
		g_free(resp);
	} else {
		sr_info("HP1660ES LA: STATE mode — skipping SPERIOD (external clock)");
	}

	/*
	 * Arming source.
	 *
	 * Default "RUN": acquisizione indipendente, sequenza normale.
	 * "MACHINE2":    LA armato dall'OSC via intermodule bus.
	 *                In questo caso NON si manda :START — l'OSC
	 *                triggherà la LA quando scatta il suo trigger.
	 *                (futura implementazione cross-trigger)
	 *
	 * Il manuale conferma: ARM {RUN|MACHINE2|INTermodule}
	 * La LA non inizia ad acquisire finché non riceve il segnale di arm.
	 */
	if (g_ascii_strcasecmp(state->la_arm_source, "MACHINE2") == 0 ||
	    g_ascii_strcasecmp(state->la_arm_source, "INTermodule") == 0) {
		char arm_cmd[48];
		g_snprintf(arm_cmd, sizeof(arm_cmd),
			":MACHINE1:ARM %s", state->la_arm_source);
		sr_info("HP1660ES LA: %s (cross-arm, no :START)", arm_cmd);
		if (sr_scpi_send(scpi, arm_cmd) != SR_OK)
			return SR_ERR;
		/* In cross-arm mode la LA aspetta il segnale dall'altro modulo.
		 * Non inviamo :RMODE SINGLE + :START — lo farà l'OSC. */
	} else {
		/* Modalità normale: acquisizione indipendente */
		if (scpi_cmd(scpi, ":RMODE SINGLE") != SR_OK)
			return SR_ERR;
		if (scpi_cmd(scpi, ":START") != SR_OK)
			return SR_ERR;
	}

	/*
	 * Avvia la macchina a stati asincrona.
	 * Il callback hp1660es_la_data_receive() gestirà:
	 *   ACQ_STATE_WAIT_ACQ  → polling *OPC? ogni 250ms (no :STOP)
	 *   ACQ_STATE_RECEIVING → accumulo blob + parsing
	 * Nessun g_usleep() in nessuna di queste fasi.
	 *
	 * Nota: ACQ_STATE_WAIT_STOP non viene più usato per LA.
	 * Il :STOP forzato impediva allo strumento di completare
	 * l'acquisizione — SYSTEM:DATA? restituiva sempre il blob
	 * precedente. Fix v16: si aspetta il trigger naturale via *OPC?.
	 */
	devc->acq_state       = ACQ_STATE_WAIT_ACQ;
	devc->acq_timer_start = g_get_monotonic_time();

	/* Assicura che il buffer sia azzerato per il nuovo ciclo */
	g_free(devc->blob_buf);
	devc->blob_buf       = NULL;
	devc->blob_len       = 0;
	devc->blob_expected  = 0;
	devc->blob_allocated = 0;

	return SR_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * hp1660es_osc_configure — sequenza SCPI per acquisizione OSC
 *
 * Sequenza collaudata (ordine critico):
 *   :SELECT 2
 *   :CHANnel{n}:RANGe / OFFSet / COUPling / PROBe
 *   :TIMebase:RANGe
 *   :TRIGger:SOURce / SLOPe / LEVel
 *   :ACQuire:TYPE NORMal
 *   :WAVeform:SOURce CHANnel1
 *   :WAVeform:FORMat BYTE
 *   :ACQuire:TYPE NORMal   ← ripetuto (quirk)
 *   :DIGitize              ← senza argomento (con argomento → -144)
 * ──────────────────────────────────────────────────────────────────────────── */

SR_PRIV int hp1660es_osc_configure(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct scope_state *state;
	struct sr_scpi_dev_inst *scpi;
	const struct scope_config *config;
	struct analog_channel_state *ach;
	char cmd[128];
	int i;

	devc   = sdi->priv;
	state  = devc->model_state;
	scpi   = sdi->conn;
	config = devc->model_config;

	if (scpi_cmd(scpi, ":SELECT 2") != SR_OK)
		return SR_ERR;
	g_usleep(200000);

	for (i = 0; i < HP1660ES_N_ANALOG_CHANNELS; i++) {
		ach = &state->analog_states[i];
		int ch_n = i + 1;

		/* RANGe totale = V/div × num_ydivs */
		float vdiv = (float)hp1660es_osc_vdivs[ach->vdiv_idx][0] /
				(float)hp1660es_osc_vdivs[ach->vdiv_idx][1];
		float range = vdiv * config->num_ydivs;

		sr_dbg("HP1660ES OSC configure CH%d: "
		       "vdiv_idx=%d vdiv=%.4fV/div num_ydivs=%d "
		       "-> RANGe=%.6fV sent",
		       ch_n, ach->vdiv_idx, vdiv, config->num_ydivs, range);

		g_snprintf(cmd, sizeof(cmd), ":CHANnel%d:RANGe %g", ch_n, range);
		if (scpi_cmd(scpi, cmd) != SR_OK) return SR_ERR;
		if (sr_scpi_get_opc(scpi) != SR_OK) return SR_ERR;

		g_snprintf(cmd, sizeof(cmd), ":CHANnel%d:OFFSet %g",
				ch_n, ach->vertical_offset);
		if (scpi_cmd(scpi, cmd) != SR_OK) return SR_ERR;
		if (sr_scpi_get_opc(scpi) != SR_OK) return SR_ERR;

		g_snprintf(cmd, sizeof(cmd), ":CHANnel%d:COUPling %s",
				ch_n, coupling_options[ach->coupling]);
		if (scpi_cmd(scpi, cmd) != SR_OK) return SR_ERR;

		g_snprintf(cmd, sizeof(cmd), ":CHANnel%d:PROBe %d",
				ch_n, ach->probe_factor);
		if (scpi_cmd(scpi, cmd) != SR_OK) return SR_ERR;
	}

	/* TIMebase:RANGe totale = T/div × num_xdivs */
	{
		float tdiv = (float)hp1660es_osc_timebases[state->osc_timebase_idx][0] /
				(float)hp1660es_osc_timebases[state->osc_timebase_idx][1];
		float range = tdiv * config->num_xdivs;
		g_snprintf(cmd, sizeof(cmd), ":TIMebase:RANGe %g", range);
		if (scpi_cmd(scpi, cmd) != SR_OK) return SR_ERR;
	}

	/* Trigger */
	g_snprintf(cmd, sizeof(cmd), ":TRIGger:SOURce %s",
			state->osc_trigger_source);
	if (scpi_cmd(scpi, cmd) != SR_OK) return SR_ERR;

	g_snprintf(cmd, sizeof(cmd), ":TRIGger:SLOPe %s",
			state->osc_trigger_slope);
	if (scpi_cmd(scpi, cmd) != SR_OK) return SR_ERR;

	g_snprintf(cmd, sizeof(cmd), ":TRIGger:LEVel %g",
			state->osc_trigger_level_v);
	if (scpi_cmd(scpi, cmd) != SR_OK) return SR_ERR;

	if (scpi_cmd(scpi, ":ACQuire:TYPE NORMal") != SR_OK) return SR_ERR;

	/* Set WAVeform:SOURce to the first enabled channel — not hardcoded CHANnel1.
	 * Bug: previously always sent CHANnel1, causing CH2-only acquisitions to
	 * return CH1 data with CH2 preamble applied → corrupted waveform. */
	{
		struct sr_channel *first_ch = devc->enabled_channels->data;
		int first_idx = first_ch->index - HP1660ES_ANALOG_CHAN_INDEX_OFFS;
		g_snprintf(cmd, sizeof(cmd), ":WAVeform:SOURce CHANnel%d", first_idx + 1);
		if (scpi_cmd(scpi, cmd) != SR_OK) return SR_ERR;
	}

	if (scpi_cmd(scpi, ":WAVeform:FORMat BYTE") != SR_OK) return SR_ERR;

	/* Quirk: ACQuire:TYPE ripetuto subito prima di DIGitize */
	if (scpi_cmd(scpi, ":ACQuire:TYPE NORMal") != SR_OK) return SR_ERR;

	/*
	 * :DIGitize e l'attesa del suo completamento sono stati spostati
	 * in hp1660es_osc_acquire() e nella state machine di
	 * hp1660es_osc_data_receive() per non bloccare il loop GLib.
	 */
	return SR_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * hp1660es_mixed_configure — sequenza SCPI per acquisizione mixed LA+OSC
 *
 * Cross-arming LA→OSC via INTermodule bus. Solo TIMING mode per ora.
 *
 * Sequenza verificata su strumento reale (IP 192.168.1.110):
 * 1. :SELECT 1 → configura LA in TIMING mode (riusa la_configure)
 * 2. :SELECT 2 → configura OSC con :TRIGger:MODE IMMEDIATE
 * 3. :INTermodule:INSert 1,GROUP  — LA armato da Group Run
 * 4. :INTermodule:INSert 2,1      — OSC armato da LA (slot 1)
 *
 * L'OSC in Immediate mode trigghera autonomamente appena riceve
 * l'arm signal dalla LA — non serve configurare il trigger OSC.
 * ──────────────────────────────────────────────────────────────────────────── */

SR_PRIV int hp1660es_mixed_configure(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct scope_state *state;
	struct sr_scpi_dev_inst *scpi;
	const struct scope_config *config;
	struct analog_channel_state *ach;
	char cmd[128];
	int i;

	devc = sdi->priv;
	state = devc->model_state;
	scpi = sdi->conn;
	config = devc->model_config;

	if (g_ascii_strcasecmp(state->la_machine_type, "STATE") == 0) {
		sr_err("HP1660ES: mixed acquisition not supported in STATE mode "
			"(only TIMING mode for now)");
		return SR_ERR_NA;
	}

	/* ── Passo 1: configura LA (SELECT 1, TYPE TIMING, LABEL, SPERIOD) ── */
	if (hp1660es_la_configure(sdi) != SR_OK)
		return SR_ERR;

	/* SPERIOD — la_configure non lo manda (lo fa la_acquire),
	 * ma per mixed va inviato ora prima di passare a SELECT 2 */
	{
		double period_s = (double)state->la_sample_period_ps / 1e12;
		char *resp;
		g_snprintf(cmd, sizeof(cmd),
			":MACHINE1:TTRIGGER:SPERIOD %.6E", period_s);
		sr_info("HP1660ES mixed: %s", cmd);
		if (sr_scpi_send(scpi, cmd) != SR_OK)
			return SR_ERR;
		if (sr_scpi_get_string(scpi, "*OPC?", &resp) != SR_OK)
			return SR_ERR;
		g_free(resp);
	}

	/* ── Passo 2: configura OSC (SELECT 2, canali, timebase, IMMEDIATE) ── */
	if (scpi_cmd(scpi, ":SELECT 2") != SR_OK)
		return SR_ERR;
	g_usleep(200000);

	for (i = 0; i < HP1660ES_N_ANALOG_CHANNELS; i++) {
		if (!state->analog_states[i].state)
			continue;
		ach = &state->analog_states[i];
		int ch_n = i + 1;

		float vdiv = (float)hp1660es_osc_vdivs[ach->vdiv_idx][0] /
			(float)hp1660es_osc_vdivs[ach->vdiv_idx][1];
		float range = vdiv * config->num_ydivs;

		g_snprintf(cmd, sizeof(cmd), ":CHANnel%d:RANGe %g", ch_n, range);
		if (scpi_cmd(scpi, cmd) != SR_OK) return SR_ERR;
		if (sr_scpi_get_opc(scpi) != SR_OK) return SR_ERR;

		g_snprintf(cmd, sizeof(cmd), ":CHANnel%d:OFFSet %g",
			ch_n, ach->vertical_offset);
		if (scpi_cmd(scpi, cmd) != SR_OK) return SR_ERR;
		if (sr_scpi_get_opc(scpi) != SR_OK) return SR_ERR;

		g_snprintf(cmd, sizeof(cmd), ":CHANnel%d:COUPling %s",
			ch_n, coupling_options[ach->coupling]);
		if (scpi_cmd(scpi, cmd) != SR_OK) return SR_ERR;

		g_snprintf(cmd, sizeof(cmd), ":CHANnel%d:PROBe %d",
			ch_n, ach->probe_factor);
		if (scpi_cmd(scpi, cmd) != SR_OK) return SR_ERR;
	}

	/* TIMebase:RANGe */
	{
		float tdiv = (float)hp1660es_osc_timebases[state->osc_timebase_idx][0] /
			(float)hp1660es_osc_timebases[state->osc_timebase_idx][1];
		float range = tdiv * config->num_xdivs;
		g_snprintf(cmd, sizeof(cmd), ":TIMebase:RANGe %g", range);
		if (scpi_cmd(scpi, cmd) != SR_OK) return SR_ERR;
	}

	/* CRITICO: trigger mode IMMEDIATE per cross-arming */
	if (scpi_cmd(scpi, ":TRIGger:MODE IMMEDIATE") != SR_OK)
		return SR_ERR;

	if (scpi_cmd(scpi, ":ACQuire:TYPE NORMal") != SR_OK)
		return SR_ERR;

	{
		struct sr_channel *first_osc = NULL;
		GSList *l;
		for (l = devc->enabled_channels; l; l = l->next) {
			struct sr_channel *ch = l->data;
			if (ch->type == SR_CHANNEL_ANALOG) {
				first_osc = ch;
				break;
			}
		}
		if (!first_osc) {
			sr_err("HP1660ES mixed: no analog channel enabled");
			return SR_ERR;
		}
		int first_idx = first_osc->index - HP1660ES_ANALOG_CHAN_INDEX_OFFS;
		g_snprintf(cmd, sizeof(cmd), ":WAVeform:SOURce CHANnel%d",
			first_idx + 1);
		if (scpi_cmd(scpi, cmd) != SR_OK) return SR_ERR;
	}

	if (scpi_cmd(scpi, ":WAVeform:FORMat BYTE") != SR_OK)
		return SR_ERR;

	/* Quirk: ACQuire:TYPE ripetuto subito prima di DIGitize
	 * (come in osc_configure). DIGitize vero è post-acquisizione
	 * in mixed_data_receive, ma il quirk repeat va qui. */
	if (scpi_cmd(scpi, ":ACQuire:TYPE NORMal") != SR_OK)
		return SR_ERR;

	/* ── Passo 3: INTermodule tree — cross-arming LA→OSC ── */
	if (scpi_cmd(scpi, ":INTermodule:INSert 1,GROUP") != SR_OK)
		return SR_ERR;
	if (scpi_cmd(scpi, ":INTermodule:INSert 2,1") != SR_OK)
		return SR_ERR;

	sr_info("HP1660ES mixed: INTermodule tree configured (LA=GROUP, OSC armed by LA)");

	/* Verifica l'albero intermodule */
	{
		char *tree_resp = NULL;
		if (sr_scpi_get_string(scpi, ":INTermodule:TTIMe?", &tree_resp)
			== SR_OK && tree_resp) {
			sr_info("HP1660ES mixed: TTIMe? = %s", tree_resp);
			g_free(tree_resp);
		}
		if (sr_scpi_get_string(scpi, ":INTermodule:TREE?", &tree_resp)
			== SR_OK && tree_resp) {
			sr_info("HP1660ES mixed: TREE? = %s", tree_resp);
			g_free(tree_resp);
		}
	}

	return SR_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * hp1660es_mixed_acquire — avvia acquisizione mixed LA+OSC
 *
 * :RMODE SINGLE + :START (Group Run arm la LA, che a sua volta arma l'OSC)
 * NON si usa :MACHINE1:ARM — l'arm è gestito dall'INTermodule tree.
 * ──────────────────────────────────────────────────────────────────────────── */

SR_PRIV int hp1660es_mixed_acquire(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;

	devc = sdi->priv;
	scpi = sdi->conn;

	if (scpi_cmd(scpi, ":RMODE SINGLE") != SR_OK)
		return SR_ERR;
	if (scpi_cmd(scpi, ":START") != SR_OK)
		return SR_ERR;

	devc->acq_state = ACQ_STATE_MIXED_WAIT_ACQ;
	devc->acq_timer_start = g_get_monotonic_time();

	g_free(devc->blob_buf);
	devc->blob_buf = NULL;
	devc->blob_len = 0;
	devc->blob_expected = 0;
	devc->blob_allocated = 0;

	sr_info("HP1660ES mixed: acquisition started (RMODE SINGLE + START)");

	return SR_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * hp1660es_osc_acquire — avvia il download waveform in modo asincrono.
 *
 * Invia :WAVeform:DATA? e chiama sr_scpi_read_begin() per aprire la
 * ricezione. Il blob viene poi accumulato chunk per chunk in
 * hp1660es_osc_data_receive() ad ogni invocazione del callback, esattamente
 * come avviene per la parte LA.
 *
 * Nota: NON si usa più hp1660es_read_blob_sync() — quella funzione bloccava
 * il loop degli eventi di sigrok e la GUI di PulseView per tutta la durata
 * del trasferimento. Il problema originale (il socket tcp-raw di libsigrok
 * segnalava read_complete=TRUE dopo soli 2 byte dell'header #9, prima che
 * arrivasse il payload) viene ora gestito correttamente nel callback
 * controllando blob_len vs blob_expected, come previsto dal pattern sigrok.
 *
 * :WAVeform:PREamble? va letto DOPO DATA? (quirk: prima xorigin = '.')
 * → viene letto in osc_data_receive una volta che il blob è completo.
 * ──────────────────────────────────────────────────────────────────────────── */

SR_PRIV int hp1660es_osc_acquire(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;
	const struct scope_config *config;
	struct scope_state *state;
	float tdiv, tb_range;
	gint64 wait_us;

	devc   = sdi->priv;
	scpi   = sdi->conn;
	config = devc->model_config;
	state  = devc->model_state;

	/* Reset buffer */
	g_free(devc->blob_buf);
	devc->blob_buf       = NULL;
	devc->blob_len       = 0;
	devc->blob_expected  = 0;
	devc->blob_allocated = 0;

	/* :DIGitize senza argomento — con argomento genera errore -144 */
	if (sr_scpi_send(scpi, ":DIGitize") != SR_OK) {
		sr_err("HP1660ES OSC: failed to send DIGitize");
		return SR_ERR;
	}

	/*
	 * Calcola il tempo di attesa per il completamento di DIGitize
	 * in base al timebase corrente. Il 1660ES non supporta *OPC?
	 * dopo DIGitize in modo affidabile — usiamo un timeout calibrato.
	 * Minimo 500ms, poi timebase_range × 2 per sicurezza, max 10s.
	 * L'attesa avviene in modo NON bloccante nella state machine
	 * (ACQ_STATE_OSC_WAIT_DIGITIZE), non qui.
	 */
	tdiv     = (float)hp1660es_osc_timebases[state->osc_timebase_idx][0] /
	           (float)hp1660es_osc_timebases[state->osc_timebase_idx][1];
	tb_range = tdiv * (float)config->num_xdivs;
	wait_us  = 500000 + (gint64)(tb_range * 1e6 * 2.0);
	if (wait_us > 10000000)
		wait_us = 10000000;  /* max 10s */

	sr_dbg("HP1660ES OSC: DIGitize sent, waiting %" G_GINT64_FORMAT " us "
	       "(non-blocking)", wait_us);

	devc->acq_state       = ACQ_STATE_OSC_WAIT_DIGITIZE;
	devc->acq_timer_start = g_get_monotonic_time();

	/*
	 * Memorizziamo il timeout nel campo blob_expected come temporaneo
	 * finché non è noto il vero payload — usiamo blob_allocated per
	 * il timeout in µs (sicuro: blob_buf è NULL in questo stato).
	 */
	devc->blob_allocated = (size_t)wait_us;

	return SR_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Blob parsing — helper
 * ──────────────────────────────────────────────────────────────────────────── */

/*
 * Legge l'header IEEE 488.2: "#NDDDD..." e ritorna la lunghezza payload.
 * Formato: '#' + N (1 cifra) + N cifre decimali della lunghezza.
 * Esempio: "#9000204576" → 204576 byte.
 * Ritorna SR_ERR se il blob non inizia con '#'.
 */
static int ieee488_header_parse(const uint8_t *buf, size_t buf_len,
		size_t *payload_len_out, size_t *header_len_out)
{
	int n;
	char s[12];
	long payload_len;

	if (buf_len < 2 || buf[0] != '#')
		return SR_ERR;

	n = buf[1] - '0';
	if (n <= 0 || n > 9 || (size_t)(2 + n) > buf_len)
		return SR_ERR;

	memcpy(s, buf + 2, n);
	s[n] = '\0';

	payload_len = strtol(s, NULL, 10);
	if (payload_len <= 0)
		return SR_ERR;

	*payload_len_out = (size_t)payload_len;
	*header_len_out  = 2 + n;

	return SR_OK;
}

SR_PRIV int hp1660es_blob_find_osc_section(const uint8_t *blob, size_t blob_len,
		const uint8_t **section_out, size_t *section_len_out,
		size_t *waveform_offset_out)
{
	size_t off, payload_len, hdr_len, sec_data_len;
	const uint8_t *payload;

	if (!blob || blob_len < HP1660ES_IEEE488_HDR_LEN)
		return SR_ERR;

	for (off = 0; off < blob_len && blob[off] != '#'; off++)
		;
	if (off >= blob_len)
		return SR_ERR;

	if (ieee488_header_parse(blob + off, blob_len - off,
			&payload_len, &hdr_len) != SR_OK)
		return SR_ERR;

	payload = blob + off + hdr_len;

	/*
	 * Scansiona le sezioni cercando la prima con module_id != MODULE_LA.
	 * In mixed mode, SYSTEM:DATA? di SELECT 2 contiene la sezione OSC.
	 * L'ID OSC non e' documentato — lo logghiamo cosi' possiamo definire
	 * HP1660ES_MODULE_OSC una volta confermato.
	 */
	off = 0;
	while (off + HP1660ES_SECTION_HDR_LEN <= payload_len) {
		uint8_t module_id;
		uint32_t sec_len;

		module_id = payload[off + HP1660ES_MODULE_ID_OFFSET];
		sec_len   = ((uint32_t)payload[off + HP1660ES_SECLEN_OFFSET    ] << 24) |
		            ((uint32_t)payload[off + HP1660ES_SECLEN_OFFSET + 1] << 16) |
		            ((uint32_t)payload[off + HP1660ES_SECLEN_OFFSET + 2] <<  8) |
		            ((uint32_t)payload[off + HP1660ES_SECLEN_OFFSET + 3]);
		sec_data_len = sec_len;

		if (module_id != HP1660ES_MODULE_LA) {
			char sec_name[11] = {0};
			memcpy(sec_name, payload + off, 10);
			sr_info("HP1660ES: OSC section found: name='%s' "
				"module_id=0x%02x sec_len=%zu",
				sec_name, module_id, sec_data_len);
			*section_out     = payload + off + HP1660ES_SECTION_HDR_LEN;
			*section_len_out = sec_data_len;
			if (waveform_offset_out)
				*waveform_offset_out = 0;
			return SR_OK;
		}

		off += HP1660ES_SECTION_HDR_LEN + sec_data_len;
	}

	sr_err("HP1660ES: OSC section not found in SYSTEM:DATA? blob "
		"(all sections have module_id=0x%02x=MODULE_LA?)",
		HP1660ES_MODULE_LA);
	return SR_ERR;
}

SR_PRIV int hp1660es_blob_find_la_section(const uint8_t *blob, size_t blob_len,
		const uint8_t **section_out, size_t *section_len_out)
{
	size_t off, payload_len, hdr_len, sec_data_len;
	const uint8_t *payload;

	if (!blob || blob_len < HP1660ES_IEEE488_HDR_LEN)
		return SR_ERR;

	/* Trova '#' — potrebbe non essere al byte 0 */
	for (off = 0; off < blob_len && blob[off] != '#'; off++)
		;

	if (off >= blob_len)
		return SR_ERR;

	if (ieee488_header_parse(blob + off, blob_len - off,
			&payload_len, &hdr_len) != SR_OK)
		return SR_ERR;

	payload = blob + off + hdr_len;

	/*
	 * Scansiona le sezioni. Ogni sezione ha un header di 16 byte:
	 *   byte  0.. 9 : nome sezione ASCII
	 *   byte 10     : reserved
	 *   byte 11     : module ID
	 *   byte 12..15 : lunghezza sezione dati (uint32 BE)
	 */
	off = 0;
	while (off + HP1660ES_SECTION_HDR_LEN <= payload_len) {
		uint8_t module_id;
		uint32_t sec_len;

		module_id  = payload[off + HP1660ES_MODULE_ID_OFFSET];
		sec_len    = ((uint32_t)payload[off + HP1660ES_SECLEN_OFFSET    ] << 24) |
		             ((uint32_t)payload[off + HP1660ES_SECLEN_OFFSET + 1] << 16) |
		             ((uint32_t)payload[off + HP1660ES_SECLEN_OFFSET + 2] <<  8) |
		             ((uint32_t)payload[off + HP1660ES_SECLEN_OFFSET + 3]);

		sec_data_len = sec_len;

		if (module_id == HP1660ES_MODULE_LA) {
			*section_out     = payload + off + HP1660ES_SECTION_HDR_LEN;
			*section_len_out = sec_data_len;
			return SR_OK;
		}

		off += HP1660ES_SECTION_HDR_LEN + sec_data_len;
	}

	sr_err("HP1660ES: LA section (module_id=0x20) not found in blob");
	return SR_ERR;
}

SR_PRIV int hp1660es_la_parse_preamble(const uint8_t *preamble,
		uint64_t *sample_period_ps_out,
		uint8_t  *n_pod_pairs_out,
		uint32_t *valid_rows_out)
{
	uint64_t period_ps;
	int i;

	if (!preamble)
		return SR_ERR_ARG;

	*n_pod_pairs_out = preamble[HP1660ES_PRE_OFF_N_POD_PAIRS];

	/* sample_period_ps: uint64 big-endian a offset 16 */
	period_ps = 0;
	for (i = 0; i < 8; i++)
		period_ps = (period_ps << 8) | preamble[HP1660ES_PRE_OFF_SAMPLE_PS + i];
	*sample_period_ps_out = period_ps;

	/*
	 * valid_rows per pod: 13 × uint16 BE a offset 84.
	 * Usiamo il primo valore non-zero come conteggio campioni.
	 * In pratica per timing mode tutti i pod hanno lo stesso valid_rows.
	 */
	*valid_rows_out = 0;
	for (i = 0; i < 13; i++) {
		uint32_t vr = ((uint32_t)preamble[HP1660ES_PRE_OFF_VALID_ROWS + i * 2] << 8) |
		               preamble[HP1660ES_PRE_OFF_VALID_ROWS + i * 2 + 1];
		if (vr > 0) {
			*valid_rows_out = vr;
			break;
		}
	}

	return SR_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * hp1660es_la_samples_send
 *
 * Converte il blob LA in pacchetti SR_DF_LOGIC e li manda alla session bus.
 *
 * Il formato SR_DF_LOGIC richiede unitsize = ceil(n_channels / 8) byte per
 * campione, con i bit dei canali compattati LSB-first.
 *
 * Compattazione pod abilitati:
 *   Vengono inclusi nell'output SOLO i pod che hanno almeno un canale
 *   abilitato (pod_states[].enabled == TRUE). I pod non abilitati vengono
 *   saltati completamente. unitsize = n_pod_attivi * 2.
 *
 *   Esempio: --channels A1.0,A1.1,A8.3
 *     → pod 0 (A1) abilitato, pod 7 (A8) abilitato
 *     → unitsize = 4 (2 pod × 2 byte)
 *     → byte 0-1: dati pod A1, byte 2-3: dati pod A8
 *
 *   PulseView vede i canali nell'ordine compattato, che corrisponde
 *   esattamente all'ordine in cui sigrok ha registrato i canali abilitati.
 *
 * Quirk hardware:
 *   - Pod A (pod_idx=0): row[17]=LSB(sub-pod1), row[16]=MSB(sub-pod2)
 *   - Altri pod: row[blob_off]=MSB, row[blob_off+1]=LSB
 *   - Pod nel blob in ordine H..A (invertito) → riordinati A..H in output
 * ──────────────────────────────────────────────────────────────────────────── */

SR_PRIV int hp1660es_la_samples_send(const uint8_t *data, size_t data_len,
		uint32_t n_rows, struct sr_dev_inst *sdi)
{
	struct sr_datafeed_logic logic;
	struct sr_datafeed_packet packet;
	uint8_t *out_buf;
	uint32_t row;
	size_t out_size;
	const uint8_t *row_ptr;
	uint8_t *out_ptr;

	/* Lista pod abilitati (in ordine A..H) */
	uint8_t active_pods[HP1660ES_N_PODS];
	int n_active_pods, i;
	int blob_off;
	uint32_t pod;

	/*
	 * Costruisce la lista dei pod attivi in ordine A..H.
	 * Un pod è attivo se almeno uno dei suoi 16 canali è abilitato.
	 *
	 * NOTA: non usiamo pod_states[].enabled perché sigrok-cli può
	 * chiamare config_channel_set() in ordine non deterministico
	 * (prima disabilita tutti, poi abilita i selezionati), rendendo
	 * pod_states[].enabled inaffidabile al momento dell'acquisizione.
	 * Leggiamo direttamente digital_states[] che riflette lo stato
	 * finale di ogni singolo canale.
	 */
	n_active_pods = 0;
	{
		gboolean pod_seen[HP1660ES_N_PODS];
		GSList *l;
		struct sr_channel *ch;
		int pod_idx;
		for (i = 0; i < HP1660ES_N_PODS; i++)
			pod_seen[i] = FALSE;
		for (l = sdi->channels; l; l = l->next) {
			ch = l->data;
			if (ch->type != SR_CHANNEL_LOGIC || !ch->enabled)
				continue;
			pod_idx = (ch->index - HP1660ES_DIG_CHAN_INDEX_OFFS)
				  / HP1660ES_BITS_PER_POD;
			if (pod_idx >= 0 && pod_idx < HP1660ES_N_PODS)
				pod_seen[pod_idx] = TRUE;
		}
		for (i = 0; i < HP1660ES_N_PODS; i++) {
			if (pod_seen[i])
				active_pods[n_active_pods++] = (uint8_t)i;
		}
	}

	if (n_active_pods == 0) {
		sr_warn("HP1660ES LA: no active pods detected, using all pods");
		for (i = 0; i < HP1660ES_N_PODS; i++)
			active_pods[i] = (uint8_t)i;
		n_active_pods = HP1660ES_N_PODS;
	}

	sr_dbg("HP1660ES LA: samples_send: %d active pod(s), unitsize=%d",
	       n_active_pods, n_active_pods * 2);

	/* unitsize = n_pod_attivi * 2 byte */
	out_size = (size_t)n_rows * (size_t)n_active_pods * 2;
	out_buf  = g_malloc(out_size);
	if (!out_buf)
		return SR_ERR_MALLOC;

	out_ptr = out_buf;

	for (row = 0; row < n_rows; row++) {
		if ((size_t)(row * HP1660ES_ROW_SIZE + HP1660ES_ROW_SIZE) > data_len)
			break;

		row_ptr = data + row * HP1660ES_ROW_SIZE;

		/* Emette solo i pod attivi, in ordine compattato */
		for (i = 0; i < n_active_pods; i++) {
			pod      = active_pods[i];
			blob_off = HP1660ES_POD_ROW_OFFSET(pod);  /* = 16 - pod*2 */

			if (pod == 0) {
				/*
				 * Pod A quirk: byte 17=LSB(sub-pod1=A1.0..A1.7),
				 *              byte 16=MSB(sub-pod2=A1.8..A1.15)
				 */
				*out_ptr++ = row_ptr[17];  /* sub-pod1 */
				*out_ptr++ = row_ptr[16];  /* sub-pod2 */
			} else {
				/*
				 * Altri pod: blob_off=MSB(sub-pod2),
				 *            blob_off+1=LSB(sub-pod1)
				 */
				*out_ptr++ = row_ptr[blob_off + 1];  /* sub-pod1 */
				*out_ptr++ = row_ptr[blob_off];      /* sub-pod2 */
			}
		}
	}

	logic.length   = out_size;
	logic.unitsize = (size_t)n_active_pods * 2;
	logic.data     = out_buf;

	packet.type    = SR_DF_LOGIC;
	packet.payload = &logic;
	sr_session_send(sdi, &packet);

	g_free(out_buf);

	return SR_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * hp1660es_osc_preamble_parse
 *
 * Parsa la risposta di :WAVeform:PREamble? (10 valori CSV).
 * Formato: format,type,points,count,xincrement,xorigin,xreference,
 *          yincrement,yorigin,yreference
 *
 * Quirk HP 1660ES (REV 02.02):
 *   - type=1 nel preamble = AVERage (non PEAK — PEAK non esiste sul 1660E)
 *   - yreference=64 con offset=0 e range simmetrico (zero fisico a 64, non 128)
 *   - yincrement include già il probe factor internamente
 *   - PREamble? va letto DOPO WAVeform:DATA?, non prima
 * ──────────────────────────────────────────────────────────────────────────── */

/* ─────────────────────────────────────────────────────────────────────────────
 * hp1660es_osc_read_meta_direct
 *
 * In mixed mode WAVeform:PREamble? ritorna stringa vuota perche' non c'e'
 * stato un :DIGitize. Legge i metadati OSC direttamente dai registri SCPI
 * individuali — disponibili indipendentemente dal preamble.
 * ──────────────────────────────────────────────────────────────────────────── */

SR_PRIV int hp1660es_osc_read_meta_direct(struct sr_scpi_dev_inst *scpi,
		struct analog_channel_state *ach)
{
	char *resp = NULL;
	float val;

	/* WAVeform:POINts? */
	if (sr_scpi_get_string(scpi, ":WAVeform:POINts?", &resp) == SR_OK && resp) {
		if (parse_float(resp, &val) == SR_OK)
			ach->num_samples = (uint32_t)(val + 0.5f);
		g_free(resp); resp = NULL;
	}

	/* TIMebase -> xincrement
	 *
	 * Quirk HP 1660E: il sample rate ADC reale NON dipende da
	 * TIMebase:RANGe/points. Per range veloci (≤ 5µs) la ADC
	 * campiona sempre a 2 GHz (500ps). Per range lenti lo
	 * strumento decima con rapporto fisso 6.5536x (32768/5000).
	 * Vedere WAVeform:PREamble? per il valore reale. */
	{
		char *rng = NULL;
		if (sr_scpi_get_string(scpi, ":TIMebase:RANGe?", &rng) == SR_OK && rng) {
			float tb_range;
			if (parse_float(rng, &tb_range) == SR_OK && ach->num_samples > 0) {
				if (tb_range <= 5e-6f)
					ach->xincrement = 5.0e-10f;  /* 2 GHz */
				else
					ach->xincrement = tb_range / 5000.0f;
			}
			g_free(rng);
		}
	}
	ach->xorigin    = 0.0f;
	ach->xreference = 0.0f;

	/* CHANnel:RANGe? → yincrement = range / 128 (8-bit ADC, quirk noto) */
	{
		char *rng = NULL;
		char ch_cmd[32];
		int ch_idx = 0; /* caller sets WAVeform:SOURce before calling */
		/* Ricava indice canale da WAVeform:SOURce? */
		if (sr_scpi_get_string(scpi, ":WAVeform:SOURce?", &resp) == SR_OK && resp) {
			/* risposta: "CHANnel1" o "CHANnel2" */
			if (g_str_has_suffix(resp, "2"))
				ch_idx = 1;
			g_free(resp); resp = NULL;
		}
		g_snprintf(ch_cmd, sizeof(ch_cmd), ":CHANnel%d:RANGe?", ch_idx + 1);
		if (sr_scpi_get_string(scpi, ch_cmd, &rng) == SR_OK && rng) {
			float ch_range;
			if (parse_float(rng, &ch_range) == SR_OK)
				ach->yincrement = ch_range / 128.0f;
			g_free(rng);
		}
	}

	/* CHANnel:OFFSet? → yorigin */
	{
		char *off_str = NULL;
		char ch_cmd[32];
		int ch_idx = 0;
		if (sr_scpi_get_string(scpi, ":WAVeform:SOURce?", &resp) == SR_OK && resp) {
			if (g_str_has_suffix(resp, "2"))
				ch_idx = 1;
			g_free(resp); resp = NULL;
		}
		g_snprintf(ch_cmd, sizeof(ch_cmd), ":CHANnel%d:OFFSet?", ch_idx + 1);
		if (sr_scpi_get_string(scpi, ch_cmd, &off_str) == SR_OK && off_str) {
			if (parse_float(off_str, &val) == SR_OK)
				ach->yorigin = val;
			g_free(off_str);
		}
	}

	ach->yreference = 64.0f;  /* quirk HP1660ES: zero fisico a 64, non 128 */

	sr_info("HP1660ES mixed OSC meta direct: points=%u xincr=%.2e "
		"yincr=%.4f yref=%.1f yorig=%.4f",
		ach->num_samples, ach->xincrement,
		ach->yincrement, ach->yreference, ach->yorigin);

	return SR_OK;
}

SR_PRIV int hp1660es_osc_preamble_parse(const char *preamble_str,
		struct analog_channel_state *ach)
{
	gchar **fields;
	int n;
	float vals[10];

	if (!preamble_str || !ach)
		return SR_ERR_ARG;

	fields = g_strsplit(preamble_str, ",", 11);
	n = 0;
	while (fields[n])
		n++;

	if (n < 10) {
		g_strfreev(fields);
		return SR_ERR;
	}

	/* Parse tutti i 10 campi */
	for (int i = 0; i < 10; i++) {
		if (parse_float(g_strstrip(fields[i]), &vals[i]) != SR_OK)
			vals[i] = 0.0f;
	}

	g_strfreev(fields);

	/* Mappa: format=0, type=1, points=2, count=3,
	 *        xincrement=4, xorigin=5, xreference=6,
	 *        yincrement=7, yorigin=8, yreference=9 */
	ach->num_samples = (uint32_t)(vals[2] + 0.5f);
	ach->xincrement  = vals[4];
	ach->xorigin     = vals[5];
	ach->xreference  = vals[6];
	ach->yincrement  = vals[7];
	ach->yorigin     = vals[8];
	ach->yreference  = vals[9];

	sr_info("HP1660ES OSC preamble: points=%u xincr=%.2e yincr=%.4f "
			"yref=%.1f yorig=%.4f",
			ach->num_samples, ach->xincrement,
			ach->yincrement, ach->yreference, ach->yorigin);

	return SR_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * hp1660es_osc_samples_send
 *
 * Converte campioni raw OSC in tensioni e li manda alla session bus.
 *
 * Formula (validata su hardware con segnale ESR70 ±4V):
 *   volt[i] = (raw[i] - yreference) * yincrement + yorigin
 *
 * Quirk: raw è int8_t signed. raw > 127 → raw - 256.
 * yincrement include già il probe factor.
 * ──────────────────────────────────────────────────────────────────────────── */

SR_PRIV int hp1660es_osc_samples_send(const uint8_t *data, uint32_t n_samples,
		const struct analog_channel_state *ach,
		struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct sr_datafeed_packet packet;
	GArray *float_data;
	uint32_t i;
	float voltage;
	int8_t raw;

	devc = sdi->priv;
	ch   = devc->current_channel->data;

	float_data = g_array_sized_new(FALSE, FALSE, sizeof(float), n_samples);

	for (i = 0; i < n_samples; i++) {
		raw = (int8_t)data[i];  /* cast signed */
		voltage = ((float)raw - ach->yreference) * ach->yincrement + ach->yorigin;
		g_array_append_val(float_data, voltage);
	}

	sr_analog_init(&analog, &encoding, &meaning, &spec, 4);
	analog.meaning->channels = g_slist_append(NULL, ch);
	analog.num_samples        = float_data->len;
	analog.data               = (float *)float_data->data;
	analog.meaning->mq        = SR_MQ_VOLTAGE;
	analog.meaning->unit      = SR_UNIT_VOLT;
	analog.meaning->mqflags   = 0;

	packet.type    = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(sdi, &packet);
	g_slist_free(analog.meaning->channels);

	g_array_free(float_data, TRUE);

	(void)devc;
	return SR_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * hp1660es_la_data_receive
 *
 * Callback asincrono per acquisizione LA.
 * Chiamato da sr_scpi_source_add() quando ci sono dati disponibili.
 *
 * Flusso:
 *   1. Polling *OPC? ogni 250ms fino a trigger completato (no :STOP)
 *   2. Invia :SYSTEM:DATA? e riceve il blob in chunk
 *   3. Quando il blob è completo: parse + send SR_DF_LOGIC
 * ──────────────────────────────────────────────────────────────────────────── */

SR_PRIV int hp1660es_la_data_receive(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct scope_state *state;
	const struct scope_config *config;
	struct sr_scpi_dev_inst *scpi;
	gint64 now;
	int chunk_len;
	size_t payload_len, hdr_len;

	(void)state;
	(void)config;

	/* Variabili usate nella fase RECEIVING */
	const uint8_t *la_section, *row_data;
	size_t la_section_len, row_data_len;
	uint64_t sample_period_ps;
	uint8_t n_pod_pairs;
	uint32_t valid_rows, n_rows;

	(void)fd;
	(void)revents;

	if (!(sdi = cb_data) || !(devc = sdi->priv))
		return FALSE;

	scpi = sdi->conn;
	now  = g_get_monotonic_time();

	switch (devc->acq_state) {

	/* ── Stato 1: polling *OPC? fino a trigger completato ── */
	case ACQ_STATE_WAIT_ACQ:
		/*
		 * Quirk HP 1660ES: :STOP forzato dopo timeout fisso impedisce
		 * allo strumento di completare l'acquisizione correttamente.
		 * SYSTEM:DATA? restituisce sempre il blob dell'acquisizione
		 * precedente se :STOP arriva prima del trigger naturale.
		 *
		 * Fix v16: niente :STOP. Si fa polling di *OPC? a intervalli
		 * di 250ms non bloccanti. Lo strumento risponde '1' solo quando
		 * il trigger è scattato e l'acquisizione è committed nel buffer
		 * interno. Timeout di sicurezza: 10s (sufficiente per segnali
		 * lenti a 100kHz).
		 *
		 * Verificato su hardware: due acquisizioni consecutive
		 * producevano blob identici con :STOP, blob diversi senza.
		 */

		/*
		 * acq_timer_start è impostato in la_acquire() al momento
		 * di :START — viene usato come riferimento assoluto per
		 * il timeout di 10s. NON viene aggiornato ad ogni poll,
		 * altrimenti il timeout non scatterebbe mai.
		 * Il throttle del poll (250ms) si ottiene confrontando
		 * la differenza modulo 250ms tra now e acq_timer_start.
		 */

		/* Timeout di sicurezza: 10s dall'avvio acquisizione */
		if (now - devc->acq_timer_start > 10000000LL) {
			sr_err("HP1660ES LA: trigger timeout (10s) — "
			       "no acquisition completed");
			goto fail;
		}

		/* Throttle: interroga *OPC? solo ogni 250ms */
		{
			gint64 elapsed = now - devc->acq_timer_start;
			if (elapsed % 250000LL > 50000LL)
				return TRUE;  /* Non è la finestra del poll */
		}

		{
			char *opc_resp = NULL;
			int opc_ok = sr_scpi_get_string(scpi, "*OPC?", &opc_resp);
			int triggered = (opc_ok == SR_OK && opc_resp &&
			                 (opc_resp[0] == '1'));
			g_free(opc_resp);

			if (!triggered)
				return TRUE;  /* Non ancora — riprova tra 250ms */
		}

		/* OPC=1: trigger completato, passa subito a SYSTEM:DATA? */
		devc->blob_allocated = HP1660ES_BLOB_INITIAL_LA;
		devc->blob_buf       = g_malloc(devc->blob_allocated);
		devc->blob_len       = 0;
		devc->blob_expected  = 0;

		if (!devc->blob_buf)
			goto fail;

		if (sr_scpi_send(scpi, ":SYSTEM:DATA?") != SR_OK)
			goto fail;

		if (sr_scpi_read_begin(scpi) != SR_OK)
			goto fail;

		devc->acq_state       = ACQ_STATE_RECEIVING;
		devc->acq_timer_start = now;
		return TRUE;

	/* ── Stato 3: accumulo blob in chunk ── */
	case ACQ_STATE_RECEIVING:
		chunk_len = sr_scpi_read_data(scpi,
				(char *)(devc->blob_buf + devc->blob_len),
				(int)(devc->blob_allocated - devc->blob_len));

		if (chunk_len < 0) {
			sr_err("HP1660ES LA: read error %d", chunk_len);
			goto fail;
		}

		devc->blob_len += (size_t)chunk_len;

		/*
		 * Appena abbiamo i primi 12 byte, parsiamo l'header IEEE 488.2
		 * per sapere quanti byte aspettarci in totale.
		 * Se il blob è più grande del buffer corrente, riallociamo
		 * esattamente la memoria necessaria (una sola volta).
		 */
		if (devc->blob_expected == 0 && devc->blob_len >= 12) {
			if (ieee488_header_parse(devc->blob_buf, devc->blob_len,
					&payload_len, &hdr_len) == SR_OK) {
				devc->blob_expected = hdr_len + payload_len;

				if (devc->blob_expected > devc->blob_allocated) {
					uint8_t *tmp = g_realloc(devc->blob_buf,
					                         devc->blob_expected);
					if (!tmp)
						goto fail;
					devc->blob_buf       = tmp;
					devc->blob_allocated = devc->blob_expected;
				}

				sr_dbg("HP1660ES LA: blob expected %zu bytes "
				       "(buffer %zu bytes)",
				       devc->blob_expected, devc->blob_allocated);
			}
		}

		/* Non ancora completo — torna e aspetta il prossimo chunk */
		/* NON usare sr_scpi_read_complete() — tcp-raw lo segnala TRUE
		 * prematuramente. Stesso quirk risolto in osc_data_receive. */
		if (devc->blob_expected == 0 ||
		    devc->blob_len < devc->blob_expected)
			return TRUE;

		/* ── Blob completo — processing ── */
		devc->acq_state = ACQ_STATE_IDLE;

		sr_dbg("HP1660ES LA: blob received %zu bytes", devc->blob_len);

		std_session_send_df_frame_begin(sdi);

		if (hp1660es_blob_find_la_section(devc->blob_buf, devc->blob_len,
				&la_section, &la_section_len) != SR_OK) {
			sr_err("HP1660ES LA: LA section not found in blob");
			goto fail;
		}

		if (la_section_len < (size_t)HP1660ES_ACQ_OFFSET) {
			sr_err("HP1660ES LA: section too short for preamble");
			goto fail;
		}

		if (hp1660es_la_parse_preamble(la_section,
				&sample_period_ps, &n_pod_pairs, &valid_rows) != SR_OK)
			goto fail;

		{
			struct scope_state *state = devc->model_state;
			state->la_sample_period_ps = sample_period_ps;
			if (sample_period_ps > 0)
				state->la_samplerate_hz =
					(uint64_t)(1e12 / sample_period_ps + 0.5);
			state->la_n_pod_pairs  = n_pod_pairs;
			state->sample_rate     = state->la_samplerate_hz;
		}

		row_data     = la_section + HP1660ES_ACQ_OFFSET;
		row_data_len = la_section_len - HP1660ES_ACQ_OFFSET;

		n_rows = (uint32_t)(row_data_len / HP1660ES_ROW_SIZE);
		if (valid_rows > 0 && valid_rows < n_rows)
			n_rows = valid_rows;

		sr_info("HP1660ES LA: %u rows  samplerate=%" PRIu64 " Hz  "
		        "period=%" PRIu64 " ps  pod_pairs=%u",
		        n_rows, devc->model_state->la_samplerate_hz,
		        sample_period_ps, n_pod_pairs);

		if (hp1660es_la_samples_send(row_data, row_data_len, n_rows, sdi)
				!= SR_OK)
			goto fail;

		std_session_send_df_frame_end(sdi);

		g_free(devc->blob_buf);
		devc->blob_buf       = NULL;
		devc->blob_len       = 0;
		devc->blob_expected  = 0;
		devc->blob_allocated = 0;

		devc->num_frames++;
		if (devc->frame_limit && devc->num_frames >= devc->frame_limit) {
			sr_dev_acquisition_stop(sdi);
			return TRUE;
		}

		/* Nuova acquisizione — la_acquire reimposta la state machine */
		if (hp1660es_la_acquire(sdi) != SR_OK)
			goto fail;

		return TRUE;

	case ACQ_STATE_IDLE:
	default:
		return TRUE;
	}

fail:
	g_free(devc->blob_buf);
	devc->blob_buf       = NULL;
	devc->blob_len       = 0;
	devc->blob_expected  = 0;
	devc->blob_allocated = 0;
	devc->acq_state      = ACQ_STATE_IDLE;
	sr_dev_acquisition_stop(sdi);
	return FALSE;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * hp1660es_osc_data_receive
 *
 * Callback asincrono per acquisizione OSC.
 * Chiamato da sr_scpi_source_add() ogni volta che ci sono dati sul socket.
 *
 * Macchina a stati:
 *   ACQ_STATE_OSC_WAIT_DIGITIZE:
 *     Attende (non bloccante) il completamento di :DIGitize usando il timeout
 *     calibrato sul timebase, memorizzato in blob_allocated da osc_acquire().
 *     Quando il timeout scade: alloca buffer (128KB), invia :WAVeform:DATA?,
 *     apre la ricezione, transisce in OSC_RECEIVING.
 *
 *   ACQ_STATE_OSC_RECEIVING:
 *     Accumula chunk in blob_buf. Appena disponibili i primi 12 byte, parsa
 *     l'header IEEE 488.2 e rialloca dinamicamente il buffer alla dimensione
 *     esatta. Quando blob_len >= blob_expected: processa, poi passa al canale
 *     successivo (o ricomincia da osc_configure+osc_acquire per multi-frame).
 * ──────────────────────────────────────────────────────────────────────────── */

SR_PRIV int hp1660es_osc_data_receive(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct scope_state *state;
	struct sr_scpi_dev_inst *scpi;
	struct sr_channel *ch;
	struct analog_channel_state *ach;
	gint64 now;
	int ch_idx, chunk_len;
	size_t hdr_len, payload_len;
	uint32_t n_samples;
	char cmd[64];
	const uint8_t *waveform_data;
	char *preamble_str;

	(void)fd;
	(void)revents;

	if (!(sdi = cb_data) || !(devc = sdi->priv))
		return FALSE;

	scpi  = sdi->conn;
	state = devc->model_state;
	now   = g_get_monotonic_time();

	switch (devc->acq_state) {

	/* ── Stato 1: attesa completamento :DIGitize (non bloccante) ── */
	case ACQ_STATE_OSC_WAIT_DIGITIZE:
		/*
		 * blob_allocated contiene il timeout in µs calcolato da
		 * osc_acquire() in base al timebase corrente.
		 */
		if (now - devc->acq_timer_start < (gint64)devc->blob_allocated)
			return TRUE;

		/*
		 * Timeout scaduto: alloca buffer iniziale (128KB),
		 * invia :WAVeform:DATA? e apre la ricezione.
		 */
		devc->blob_allocated = 128 * 1024;
		devc->blob_buf       = g_malloc(devc->blob_allocated);
		devc->blob_len       = 0;
		devc->blob_expected  = 0;

		if (!devc->blob_buf)
			goto fail;

		if (sr_scpi_send(scpi, ":WAVeform:DATA?") != SR_OK) {
			sr_err("HP1660ES OSC: failed to send WAVeform:DATA?");
			goto fail;
		}

		if (sr_scpi_read_begin(scpi) != SR_OK) {
			sr_err("HP1660ES OSC: read_begin failed");
			goto fail;
		}

		devc->acq_state = ACQ_STATE_OSC_RECEIVING;
		return TRUE;

	/* ── Stato 2: accumulo blob WAVeform:DATA? ── */
	case ACQ_STATE_OSC_RECEIVING:
		chunk_len = sr_scpi_read_data(scpi,
				(char *)(devc->blob_buf + devc->blob_len),
				(int)(devc->blob_allocated - devc->blob_len));

		if (chunk_len < 0) {
			sr_err("HP1660ES OSC: read error %d", chunk_len);
			goto fail;
		}

		if (chunk_len > 0)
			devc->blob_len += (size_t)chunk_len;

		/* Parse header e riallocazione dinamica */
		if (devc->blob_expected == 0 && devc->blob_len >= 12) {
			if (ieee488_header_parse(devc->blob_buf, devc->blob_len,
					&payload_len, &hdr_len) == SR_OK) {
				devc->blob_expected = hdr_len + payload_len;

				if (devc->blob_expected > devc->blob_allocated) {
					uint8_t *tmp = g_realloc(devc->blob_buf,
					                         devc->blob_expected);
					if (!tmp)
						goto fail;
					devc->blob_buf       = tmp;
					devc->blob_allocated = devc->blob_expected;
				}

				sr_dbg("HP1660ES OSC: blob expected %zu bytes "
				       "(buffer %zu bytes)",
				       devc->blob_expected, devc->blob_allocated);
			}
		}

		/* Blob non ancora completo */
		if (devc->blob_expected == 0 ||
		    devc->blob_len < devc->blob_expected)
			return TRUE;

		/* ── Fase B: blob completo — processing ── */

		sr_dbg("HP1660ES OSC: blob complete, %zu bytes received",
		       devc->blob_len);

		devc->acq_state = ACQ_STATE_IDLE;

		if (ieee488_header_parse(devc->blob_buf, devc->blob_len,
				&payload_len, &hdr_len) != SR_OK) {
			sr_err("HP1660ES OSC: failed to parse IEEE 488.2 header");
			goto fail;
		}

		waveform_data = devc->blob_buf + hdr_len;

		ch     = devc->current_channel->data;
		ch_idx = ch->index - HP1660ES_ANALOG_CHAN_INDEX_OFFS;
		ach    = &state->analog_states[ch_idx];

		/*
		 * Quirk CRITICO: WAVeform:PREamble? va letto DOPO WAVeform:DATA?.
		 * Prima del download, xorigin risponde '.' invece del valore reale.
		 */
		if (sr_scpi_get_string(scpi, ":WAVeform:PREamble?",
				&preamble_str) == SR_OK && preamble_str) {
			hp1660es_osc_preamble_parse(preamble_str, ach);
			g_free(preamble_str);
			/* Deriva samplerate da xincrement — ora il metadata .sr
			 * ha il valore corretto per questo timebase. */
			if (ach->xincrement > 0)
				state->sample_rate =
					(uint64_t)(1.0 / ach->xincrement + 0.5);
		}

		/* Header inviato qui, dopo che sample_rate è noto dal preamble.
		 * Solo sul primo canale per non duplicarlo in acquisizioni multi-ch. */
		if (devc->current_channel == devc->enabled_channels)
			std_session_send_df_header(sdi);

		if (devc->current_channel == devc->enabled_channels)
			std_session_send_df_frame_begin(sdi);

		n_samples = (ach->num_samples > 0) ? ach->num_samples
		                                   : (uint32_t)payload_len;
		if (n_samples > (uint32_t)payload_len)
			n_samples = (uint32_t)payload_len;

		if (hp1660es_osc_samples_send(waveform_data, n_samples,
				ach, sdi) != SR_OK)
			goto fail;

		g_free(devc->blob_buf);
		devc->blob_buf       = NULL;
		devc->blob_len       = 0;
		devc->blob_expected  = 0;
		devc->blob_allocated = 0;

		/* ── Canale successivo o nuova acquisizione ── */

		if (devc->current_channel->next) {
			/*
			 * Ci sono altri canali abilitati.
			 * Cambia sorgente WAVeform e riavvia la ricezione
			 * per il prossimo canale (re-entra in WAIT_DIGITIZE
			 * con timeout zero — DIGitize è già avvenuto).
			 */
			devc->current_channel = devc->current_channel->next;
			ch     = devc->current_channel->data;
			ch_idx = ch->index - HP1660ES_ANALOG_CHAN_INDEX_OFFS;

			g_snprintf(cmd, sizeof(cmd), ":WAVeform:SOURce CHANnel%d",
					ch_idx + 1);
			if (sr_scpi_send(scpi, cmd) != SR_OK)
				goto fail;

			/*
			 * Per il secondo canale non serve un nuovo DIGitize —
			 * i dati sono già acquisiti. Riusa osc_acquire() che
			 * manderà :WAVeform:DATA? dopo un breve timeout (500ms).
			 */
			if (hp1660es_osc_acquire(sdi) != SR_OK)
				goto fail;

		} else {
			/* Ultimo canale — fine frame */
			std_session_send_df_frame_end(sdi);
			devc->current_channel = devc->enabled_channels;
			devc->num_frames++;

			if (devc->frame_limit &&
			    devc->num_frames >= devc->frame_limit) {
				sr_dev_acquisition_stop(sdi);
				return TRUE;
			}

			/*
			 * Nuova acquisizione multi-frame:
			 * 1. Riconfigura OSC (canali, timebase, trigger)
			 * 2. osc_acquire() manda :DIGitize e avvia state machine
			 * Fix: osc_configure() non chiama più osc_acquire() da sé —
			 * dobbiamo chiamarlo esplicitamente qui.
			 */
			if (hp1660es_osc_configure(sdi) != SR_OK)
				goto fail;

			if (hp1660es_osc_acquire(sdi) != SR_OK)
				goto fail;
		}

		return TRUE;

	case ACQ_STATE_IDLE:
	default:
		return TRUE;
	}

fail:
	g_free(devc->blob_buf);
	devc->blob_buf       = NULL;
	devc->blob_len       = 0;
	devc->blob_expected  = 0;
	devc->blob_allocated = 0;
	devc->acq_state      = ACQ_STATE_IDLE;
	sr_dev_acquisition_stop(sdi);
	return FALSE;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * hp1660es_mixed_data_receive
 *
 * Callback asincrono per acquisizione mixed LA+OSC (cross-arming).
 *
 * Macchina a stati:
 * MIXED_WAIT_ACQ:
 *   Polling *OPC? ogni 250ms fino a trigger completato.
 *   Quando OPC=1: seleziona LA, invia :SYSTEM:DATA?, transisce a
 *   MIXED_RECEIVING_LA.
 *
 * MIXED_RECEIVING_LA:
 *   Accumula blob LA. Quando completo: parse + send SR_DF_LOGIC.
 *   Poi seleziona OSC, invia :WAVeform:DATA? per il primo canale,
 *   transisce a MIXED_RECEIVING_OSC.
 *
 * MIXED_RECEIVING_OSC:
 *   Accumula blob OSC. Quando completo: parse preamble + send SR_DF_ANALOG.
 *   Se ci sono altri canali analogici: cambia sorgente WAVeform,
 *   invia nuovo DATA?, resta in MIXED_RECEIVING_OSC.
 *   Altrimenti: fine frame, clean intermodule tree, ricomincia.
 * ──────────────────────────────────────────────────────────────────────────── */

SR_PRIV int hp1660es_mixed_data_receive(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct scope_state *state;
	const struct scope_config *config;
	struct sr_scpi_dev_inst *scpi;
	gint64 now;
	int chunk_len;
	size_t payload_len, hdr_len;

	(void)fd;
	(void)revents;

	if (!(sdi = cb_data) || !(devc = sdi->priv))
		return FALSE;

	scpi = sdi->conn;
	state = devc->model_state;
	config = devc->model_config;
	now = g_get_monotonic_time();

	switch (devc->acq_state) {

	case ACQ_STATE_MIXED_WAIT_ACQ:
		if (now - devc->acq_timer_start > 10000000LL) {
			sr_err("HP1660ES mixed: trigger timeout (10s)");
			goto fail;
		}
		{
			gint64 elapsed = now - devc->acq_timer_start;
			if (elapsed % 250000LL > 50000LL)
				return TRUE;
		}
		{
			char *opc_resp = NULL;
			int opc_ok = sr_scpi_get_string(scpi, "*OPC?", &opc_resp);
			int triggered = (opc_ok == SR_OK && opc_resp &&
				(opc_resp[0] == '1'));
			g_free(opc_resp);
			if (!triggered)
				return TRUE;
		}

		sr_info("HP1660ES mixed: both modules triggered — downloading LA data");

		if (scpi_cmd(scpi, ":SELECT 1") != SR_OK)
			goto fail;

		devc->blob_allocated = HP1660ES_BLOB_INITIAL_LA;
		devc->blob_buf = g_malloc(devc->blob_allocated);
		devc->blob_len = 0;
		devc->blob_expected = 0;
		if (!devc->blob_buf)
			goto fail;

		if (sr_scpi_send(scpi, ":SYSTEM:DATA?") != SR_OK)
			goto fail;
		if (sr_scpi_read_begin(scpi) != SR_OK)
			goto fail;

		devc->acq_state = ACQ_STATE_MIXED_RECEIVING_LA;
		devc->acq_timer_start = now;
		return TRUE;

	case ACQ_STATE_MIXED_RECEIVING_LA:
		chunk_len = sr_scpi_read_data(scpi,
			(char *)(devc->blob_buf + devc->blob_len),
			(int)(devc->blob_allocated - devc->blob_len));

		if (chunk_len < 0) {
			sr_err("HP1660ES mixed LA: read error %d", chunk_len);
			goto fail;
		}

		devc->blob_len += (size_t)chunk_len;

		if (devc->blob_expected == 0 && devc->blob_len >= 12) {
			if (ieee488_header_parse(devc->blob_buf, devc->blob_len,
				&payload_len, &hdr_len) == SR_OK) {
				devc->blob_expected = hdr_len + payload_len;
				if (devc->blob_expected > devc->blob_allocated) {
					uint8_t *tmp = g_realloc(devc->blob_buf,
						devc->blob_expected);
					if (!tmp) goto fail;
					devc->blob_buf = tmp;
					devc->blob_allocated = devc->blob_expected;
				}
			}
		}

		if (devc->blob_expected == 0 ||
			devc->blob_len < devc->blob_expected)
			return TRUE;

		/* ── LA blob fully received ── */

		/* Finish the current SCPI read *and* reset the state machine to IDLE
		   so that spurious callbacks during the upcoming configuration phase
		   are ignored. */
		sr_scpi_read_complete(scpi);
		devc->acq_state = ACQ_STATE_IDLE;   /* ← critical fix */

		sr_info("HP1660ES mixed: LA data received, %zu bytes total", devc->blob_len);

		/* Dump sections (debug info) */
		{
			size_t off2 = 0;
			size_t pl, hl;
			if (ieee488_header_parse(devc->blob_buf, devc->blob_len,
				&pl, &hl) == SR_OK) {
				const uint8_t *payload2 = devc->blob_buf + hl;
				size_t plen2 = pl;
				while (off2 + 16 <= plen2) {
					uint8_t mod_id = payload2[off2 + 11];
					uint32_t sec_len =
						((uint32_t)payload2[off2+12] << 24) |
						((uint32_t)payload2[off2+13] << 16) |
						((uint32_t)payload2[off2+14] <<  8) |
						((uint32_t)payload2[off2+15]);
					char sec_name[11] = {0};
					memcpy(sec_name, payload2 + off2, 10);
					sr_info("HP1660ES mixed blob: off=%zu name='%s' "
						"mod_id=0x%02x sec_len=%u",
						off2, sec_name, mod_id, sec_len);
					if (sec_len == 0) break;
					off2 += 16 + sec_len;
				}
			}
		}
		{
			const uint8_t *la_section, *row_data;
			size_t la_section_len, row_data_len;
			uint64_t sample_period_ps;
			uint8_t n_pod_pairs;
			uint32_t valid_rows, n_rows;

			if (hp1660es_blob_find_la_section(devc->blob_buf,
				devc->blob_len, &la_section,
				&la_section_len) != SR_OK) {
				sr_err("HP1660ES mixed: LA section not found");
				goto fail;
			}

			if (la_section_len < (size_t)HP1660ES_ACQ_OFFSET) {
				sr_err("HP1660ES mixed: LA section too short");
				goto fail;
			}

			if (hp1660es_la_parse_preamble(la_section,
				&sample_period_ps, &n_pod_pairs,
				&valid_rows) != SR_OK)
				goto fail;

			state->la_sample_period_ps = sample_period_ps;
			if (sample_period_ps > 0)
				state->la_samplerate_hz =
					(uint64_t)(1e12 / sample_period_ps + 0.5);
			state->la_n_pod_pairs = n_pod_pairs;
			state->sample_rate = state->la_samplerate_hz;

			row_data = la_section + HP1660ES_ACQ_OFFSET;
			row_data_len = la_section_len - HP1660ES_ACQ_OFFSET;
			n_rows = (uint32_t)(row_data_len / HP1660ES_ROW_SIZE);
			if (valid_rows > 0 && valid_rows < n_rows)
				n_rows = valid_rows;

			sr_info("HP1660ES mixed LA: %u rows sr=%" PRIu64 " Hz",
				n_rows, state->la_samplerate_hz);

			std_session_send_df_frame_begin(sdi);

			if (hp1660es_la_samples_send(row_data, row_data_len,
				n_rows, sdi) != SR_OK)
				goto fail;
		}

		sr_info("HP1660ES mixed: LA data sent — preparing OSC download");

		/* Free LA buffer */
		g_free(devc->blob_buf);
		devc->blob_buf = NULL;
		devc->blob_len = 0;
		devc->blob_expected = 0;
		devc->blob_allocated = 0;

		/* --- Switch to oscilloscope module --- */
		if (scpi_cmd(scpi, ":SELECT 2") != SR_OK)
			goto fail;

		{
			struct sr_channel *first_osc = NULL;
			GSList *l;
			for (l = devc->enabled_channels; l; l = l->next) {
				struct sr_channel *ch = l->data;
				if (ch->type == SR_CHANNEL_ANALOG) {
					first_osc = ch;
					break;
				}
			}
			if (!first_osc) {
				sr_err("HP1660ES mixed: no analog channel found");
				goto fail;
			}
			devc->current_channel = g_slist_find(
				devc->enabled_channels, first_osc);

			char cmd[64];
			int ch_idx = first_osc->index - HP1660ES_ANALOG_CHAN_INDEX_OFFS;
			g_snprintf(cmd, sizeof(cmd),
				":WAVeform:SOURce CHANnel%d", ch_idx + 1);
			if (sr_scpi_send(scpi, cmd) != SR_OK)
				goto fail;
		}

		/* Set waveform format */
		if (sr_scpi_send(scpi, ":WAVeform:FORMat BYTE") != SR_OK)
			goto fail;

		/* --- Preamble query (as in the working Python script) --- */
		{
			char *pre_str = NULL;
			if (sr_scpi_get_string(scpi, ":WAVeform:PREamble?", &pre_str) == SR_OK
				&& pre_str) {
				sr_dbg("HP1660ES mixed: OSC preamble = %.80s", pre_str);
				devc->mixed_osc_preamble = pre_str;  /* will be freed later */
			} else {
				sr_warn("HP1660ES mixed: WAVeform:PREamble? returned empty");
				devc->mixed_osc_preamble = NULL;
			}
		}

		/* Now request the waveform data */
		sr_dbg("HP1660ES mixed: starting WAVeform:DATA?");

		devc->blob_allocated = HP1660ES_BLOB_INITIAL_OSC;
		devc->blob_buf = g_malloc(devc->blob_allocated);
		devc->blob_len = 0;
		devc->blob_expected = 0;
		devc->mixed_osc_extra_byte = 1;   /* trailing \n after the blob */
		if (!devc->blob_buf)
			goto fail;

		if (sr_scpi_send(scpi, ":WAVeform:DATA?") != SR_OK)
			goto fail;
		if (sr_scpi_read_begin(scpi) != SR_OK)
			goto fail;

		/* Only now go back to a receiving state */
		devc->acq_state = ACQ_STATE_MIXED_RECEIVING_OSC;
		return TRUE;

	case ACQ_STATE_MIXED_RECEIVING_OSC:
		chunk_len = sr_scpi_read_data(scpi,
			(char *)(devc->blob_buf + devc->blob_len),
			(int)(devc->blob_allocated - devc->blob_len));

		if (chunk_len < 0) {
			sr_err("HP1660ES mixed OSC: read error %d", chunk_len);
			goto fail;
		}

		if (chunk_len > 0)
			devc->blob_len += (size_t)chunk_len;

		/* Skip any leading non‑'#' bytes (stray newlines etc.) before trying
		   to parse the IEEE‑488.2 header – exactly like the Python script does. */
		if (devc->blob_expected == 0 && devc->blob_len > 0) {
			size_t skip = 0;
			while (skip < devc->blob_len && devc->blob_buf[skip] != '#')
				skip++;
			if (skip > 0) {
				sr_dbg("HP1660ES mixed OSC: skipping %zu stray bytes before blob", skip);
				memmove(devc->blob_buf, devc->blob_buf + skip, devc->blob_len - skip);
				devc->blob_len -= skip;
			}
		}

		if (devc->blob_expected == 0 && devc->blob_len >= 12) {
			if (ieee488_header_parse(devc->blob_buf, devc->blob_len,
				&payload_len, &hdr_len) == SR_OK) {
				devc->blob_expected = hdr_len + payload_len +
					(devc->mixed_osc_extra_byte ? 1 : 0);
				if (devc->blob_expected > devc->blob_allocated) {
					uint8_t *tmp = g_realloc(devc->blob_buf,
						devc->blob_expected);
					if (!tmp) goto fail;
					devc->blob_buf = tmp;
					devc->blob_allocated = devc->blob_expected;
				}
			}
		}

		if (devc->blob_expected == 0 ||
			devc->blob_len < devc->blob_expected)
			return TRUE;

		/* ── OSC waveform blob complete ── */

		/* Finish the current SCPI read and return to IDLE to avoid re‑entry */
		sr_scpi_read_complete(scpi);
		devc->acq_state = ACQ_STATE_IDLE;

		sr_dbg("HP1660ES mixed: OSC blob complete, %zu bytes received", devc->blob_len);

		{
			struct sr_channel *ch;
			struct analog_channel_state *ach;
			int ch_idx;
			uint32_t n_samples;
			const uint8_t *waveform_data;

			if (ieee488_header_parse(devc->blob_buf, devc->blob_len,
				&payload_len, &hdr_len) != SR_OK) {
				sr_err("HP1660ES mixed OSC: WAVeform header parse failed");
				goto fail;
			}

			waveform_data = devc->blob_buf + hdr_len;

			ch = devc->current_channel->data;
			ch_idx = ch->index - HP1660ES_ANALOG_CHAN_INDEX_OFFS;
			ach = &state->analog_states[ch_idx];

			/* Use preamble if we got one, otherwise read meta directly */
			if (devc->mixed_osc_preamble && strlen(devc->mixed_osc_preamble) > 5) {
				hp1660es_osc_preamble_parse(devc->mixed_osc_preamble, ach);
				g_free(devc->mixed_osc_preamble);
				devc->mixed_osc_preamble = NULL;
			} else {
				sr_dbg("HP1660ES mixed: reading OSC meta directly");
				hp1660es_osc_read_meta_direct(scpi, ach);
			}

			n_samples = (ach->num_samples > 0)
				? ach->num_samples : (uint32_t)payload_len;
			if (n_samples > (uint32_t)payload_len)
				n_samples = (uint32_t)payload_len;

			sr_info("HP1660ES mixed OSC CH%d: %u samples sr=%.0f Hz",
				ch_idx + 1, (unsigned int)n_samples,
				(ach->xincrement > 0.0f) ? (1.0f / ach->xincrement) : 0.0f);

			if (hp1660es_osc_samples_send(waveform_data,
				n_samples, ach, sdi) != SR_OK)
				goto fail;
		}

		/* Free OSC buffer */
		g_free(devc->blob_buf);
		devc->blob_buf = NULL;
		devc->blob_len = 0;
		devc->blob_expected = 0;
		devc->blob_allocated = 0;

		/* --- Next analog channel? --- */
		{
			GSList *next_ch = devc->current_channel->next;
			GSList *l;
			struct sr_channel *next_osc = NULL;

			for (l = next_ch; l; l = l->next) {
				struct sr_channel *ch = l->data;
				if (ch->type == SR_CHANNEL_ANALOG) {
					next_osc = ch;
					break;
				}
			}

			if (next_osc) {
				char cmd[64];
				int ch_idx = next_osc->index - HP1660ES_ANALOG_CHAN_INDEX_OFFS;
				devc->current_channel =
					g_slist_find(devc->enabled_channels, next_osc);
				g_snprintf(cmd, sizeof(cmd),
					":WAVeform:SOURce CHANnel%d", ch_idx + 1);
				if (sr_scpi_send(scpi, cmd) != SR_OK)
					goto fail;

				/* For the next channel, we don't need another preamble;
				   just request the data again. */
				devc->mixed_osc_extra_byte = 1;
				devc->blob_allocated = HP1660ES_BLOB_INITIAL_OSC;
				devc->blob_buf = g_malloc(devc->blob_allocated);
				devc->blob_len = 0;
				devc->blob_expected = 0;
				if (!devc->blob_buf)
					goto fail;

				if (sr_scpi_send(scpi, ":WAVeform:DATA?") != SR_OK)
					goto fail;
				if (sr_scpi_read_begin(scpi) != SR_OK)
					goto fail;

				devc->acq_state = ACQ_STATE_MIXED_RECEIVING_OSC;
				return TRUE;
			}
		}

		/* Frame complete */
		std_session_send_df_frame_end(sdi);

		sr_info("HP1660ES mixed: frame finished — cleaning intermodule tree");
		scpi_cmd(scpi, ":INTermodule:DELete ALL");

		devc->num_frames++;
		if (devc->frame_limit && devc->num_frames >= devc->frame_limit) {
			sr_dev_acquisition_stop(sdi);
			return TRUE;
		}

		/* Start next mixed acquisition frame */
		if (hp1660es_mixed_configure(sdi) != SR_OK)
			goto fail;
		if (hp1660es_mixed_acquire(sdi) != SR_OK)
			goto fail;

		return TRUE;

	case ACQ_STATE_IDLE:
	default:
		return TRUE;
	}

fail:
	g_free(devc->blob_buf);
	devc->blob_buf = NULL;
	devc->blob_len = 0;
	devc->blob_expected = 0;
	devc->blob_allocated = 0;
	g_free(devc->mixed_osc_preamble);
	devc->mixed_osc_preamble = NULL;
	devc->acq_state = ACQ_STATE_IDLE;
	scpi_cmd(scpi, ":INTermodule:DELete ALL");
	sr_dev_acquisition_stop(sdi);
	return FALSE;
}/* ─────────────────────────────────────────────────────────────────────────────
 * scope_state_dump — debug log
 * ──────────────────────────────────────────────────────────────────────────── */

SR_PRIV void hp1660es_scope_state_dump(const struct scope_config *config,
		const struct scope_state *state)
{
	int i;
	char *tmp;

	sr_info("HP1660ES state dump:");
	sr_info("  Model: %s  FW: %s", config->model_name, config->firmware_ver);

	sr_info("  LA: type=%s  acqmode=%s  samplerate=%" PRIu64 " Hz",
			state->la_machine_type, state->la_acqmode,
			state->la_samplerate_hz);

	if (g_ascii_strcasecmp(state->la_machine_type, "STATE") == 0) {
		sr_info("  LA STATE: clock=%s,%s  sethold=%d  arm=%s",
			state->la_state_clock_id,
			state->la_state_clock_spec,
			state->la_state_sethold,
			state->la_arm_source);
	}

	for (i = 0; i < HP1660ES_N_PODS; i++) {
		sr_info("  POD A%d: threshold=%.2fV  preset=%d",
				i + 1,
				state->pod_states[i].threshold_v,
				state->pod_states[i].threshold_preset);
	}

	for (i = 0; i < HP1660ES_N_ANALOG_CHANNELS; i++) {
		tmp = sr_voltage_string(
				hp1660es_osc_vdivs[state->analog_states[i].vdiv_idx][0],
				hp1660es_osc_vdivs[state->analog_states[i].vdiv_idx][1]);
		sr_info("  OSC CH%d: %s  coupling=%d  offset=%.3fV  probe=%dx",
				i + 1, tmp,
				state->analog_states[i].coupling,
				state->analog_states[i].vertical_offset,
				state->analog_states[i].probe_factor);
		g_free(tmp);
	}

	tmp = sr_period_string(
			hp1660es_osc_timebases[state->osc_timebase_idx][0],
			hp1660es_osc_timebases[state->osc_timebase_idx][1]);
	sr_info("  OSC timebase: %s/div  delay=%.6fs  mode=%s",
			tmp, state->osc_timebase_delay_s, state->osc_timebase_mode);
	g_free(tmp);

	sr_info("  OSC trigger: %s  %s  %.3fV",
			state->osc_trigger_source,
			state->osc_trigger_slope,
			state->osc_trigger_level_v);

	sr_info("  ACQuire: type=%s  count=%d",
			state->osc_acq_type, state->osc_acq_count);
}
