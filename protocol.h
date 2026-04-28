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

#ifndef LIBSIGROK_HARDWARE_HP1660ES_PROTOCOL_H
#define LIBSIGROK_HARDWARE_HP1660ES_PROTOCOL_H

#include <glib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "hp1660es"

/* ─────────────────────────────────────────────────────────────────────────────
 * Model matching
 *
 * IDN risponde: "HEWLETT PACKARD,1660E,0,REV 02.02"
 * Il campo model è '1660E' — il suffisso 'S' NON è presente.
 * Usiamo prefix matching bidirezionale per coprire le varianti della famiglia.
 * ──────────────────────────────────────────────────────────────────────────── */

#define HP166X_MANUFACTURER_ID  "HEWLETT PACKARD"
#define HP166X_MANUFACTURER_ALT "HEWLETT-PACKARD"

/* Numero massimo di model ID per entry nella tabella scope_models[] */
#define MAX_MODEL_ID_LEN  16

/* ─────────────────────────────────────────────────────────────────────────────
 * Channel layout
 *
 * HP 1660ES: 8 pod LA (A..H) × 16 bit = 128 canali digitali
 *            2 canali analogici OSC
 *
 * Indici globali:
 *   canali digitali : 0..127   (DIG_CHAN_INDEX_OFFS = 0)
 *   canali analogici: 128..129 (ANALOG_CHAN_INDEX_OFFS = 128)
 *
 * Differenza vs DLM: DLM_DIG_CHAN_INDEX_OFFS = 32, noi = 0.
 * ──────────────────────────────────────────────────────────────────────────── */

#define HP1660ES_DIG_CHAN_INDEX_OFFS     0
#define HP1660ES_ANALOG_CHAN_INDEX_OFFS  128

#define HP1660ES_N_PODS             8    /* pod A..H */
#define HP1660ES_BITS_PER_POD       16   /* 16 bit per pod (2 sub-pod da 8) */
#define HP1660ES_N_DIG_CHANNELS     (HP1660ES_N_PODS * HP1660ES_BITS_PER_POD)  /* 128 */
#define HP1660ES_N_ANALOG_CHANNELS  2    /* CH1, CH2 */

/* ─────────────────────────────────────────────────────────────────────────────
 * Blob SYSTEM:DATA? — costanti di parsing
 *
 * Formato: [IEEE 488.2 header 10B] [section header 16B] [preamble 160B]
 *          [extra 10B] [data rows N×18B]
 *
 * Section header:
 *   byte  0.. 9 : nome sezione ASCII "DATA      "
 *   byte 10     : reserved (0x00)
 *   byte 11     : module ID (0x20 = LA)
 *   byte 12..15 : lunghezza sezione (uint32 BE)
 *
 * Preamble (160 byte, offset relativo all'inizio del preamble):
 *   offset  3   : n_pod_pairs (uint8)
 *   offset  4   : mode analyzer (uint8)
 *   offset 16   : sample_period in picosecondi (uint64 BE)
 *   offset 84   : valid_rows per pod (13 × uint16 BE)
 *   offset 110  : trigger_row per pod (13 × uint16 BE)
 *
 * Row layout (18 byte, big-endian):
 *   byte  0.. 1 : clock lines
 *   byte  2.. 3 : pod A8 (pod 8)
 *   byte  4.. 5 : pod A7 (pod 7)
 *   byte  6.. 7 : pod A6 (pod 6)
 *   byte  8.. 9 : pod A5 (pod 5)
 *   byte 10..11 : pod A4 (pod 4)
 *   byte 12..13 : pod A3 (pod 3)
 *   byte 14..15 : pod A2 (pod 2)
 *   byte 16..17 : pod A1 (pod 1) — QUIRK: byte 17=LSB, byte 16=MSB (invertito)
 *
 * Quirk TCP: 1 byte \x0a finale in meno rispetto a FTP — ignorato nel parsing.
 * ──────────────────────────────────────────────────────────────────────────── */

#define HP1660ES_IEEE488_HDR_LEN    10   /* "#8XXXXXXXX" */
#define HP1660ES_SECTION_HDR_LEN    16
#define HP1660ES_MODULE_ID_OFFSET   11   /* nel section header */
#define HP1660ES_SECLEN_OFFSET      12   /* nel section header */
#define HP1660ES_MODULE_LA          0x20

#define HP1660ES_PREAMBLE_LEN       160
#define HP1660ES_PREAMBLE_EXTRA     0 // i 10 forse erano sbagliati, nel manuale HP e' esattamente 160. non 160 + 10
#define HP1660ES_ACQ_OFFSET         (HP1660ES_PREAMBLE_LEN + HP1660ES_PREAMBLE_EXTRA)
#define HP1660ES_ROW_SIZE           18   /* byte per campione (tutti i pod) */

/* Offset nel preamble */
#define HP1660ES_PRE_OFF_N_POD_PAIRS  3
#define HP1660ES_PRE_OFF_MODE         4
#define HP1660ES_PRE_OFF_SAMPLE_PS   16  /* uint64 BE, period in picosecondi */
#define HP1660ES_PRE_OFF_VALID_ROWS  84  /* 13 × uint16 BE */
#define HP1660ES_PRE_OFF_TRIG_ROWS  110  /* 13 × uint16 BE */

/* Offset dei pod nella row (byte 0..1 = clock, poi pod H..A a scendere) */
/* pod_row_offset[pod_idx] = byte offset nella row per il pod (0=A..7=H) */
/* Pod A è agli ultimi 2 byte (offset 16), Pod H ai byte 2..3 (offset 2)  */
#define HP1660ES_POD_ROW_OFFSET(pod_idx)  (16 - (pod_idx) * 2)

/* ─────────────────────────────────────────────────────────────────────────────
 * Samplerate LA — 12 valori da 500MHz a 100kHz
 * Formato: (period_num, period_den) in secondi, come dlm_timebases
 * Inviato a strumento come: SPERIOD <period_num>e-<exp>
 * ──────────────────────────────────────────────────────────────────────────── */

#define HP1660ES_NUM_LA_SAMPLERATES  12

/* Tabella esterna definita in protocol.c */
extern const uint64_t hp1660es_la_samplerates[HP1660ES_NUM_LA_SAMPLERATES][2];

/* ─────────────────────────────────────────────────────────────────────────────
 * Timebases OSC — 31 valori
 * Formato: (num, den) in secondi/div
 * ──────────────────────────────────────────────────────────────────────────── */

#define HP1660ES_NUM_OSC_TIMEBASES  31

extern const uint64_t hp1660es_osc_timebases[HP1660ES_NUM_OSC_TIMEBASES][2];

/* ─────────────────────────────────────────────────────────────────────────────
 * V/div OSC — 14 valori
 * ──────────────────────────────────────────────────────────────────────────── */

#define HP1660ES_NUM_OSC_VDIVS  14

extern const uint64_t hp1660es_osc_vdivs[HP1660ES_NUM_OSC_VDIVS][2];

/*
 * Dimensione buffer ricezione blob — allocazione dinamica.
 * Il buffer parte da HP1660ES_BLOB_INITIAL_SIZE e viene riallocato con
 * g_realloc() alla dimensione esatta non appena l'header IEEE 488.2
 * rivela il payload_len. Non esiste più un limite fisso superiore.
 */
#define HP1660ES_BLOB_INITIAL_LA   (64 * 1024)   /* 64KB — LA iniziale  */
#define HP1660ES_BLOB_INITIAL_OSC  (128 * 1024)  /* 128KB — OSC iniziale */

/* ─────────────────────────────────────────────────────────────────────────────
 * struct analog_channel_state
 * Stato dinamico di un canale analogico OSC.
 * Speculare ad AnalogChannelState in hp1660_device.py.
 * ──────────────────────────────────────────────────────────────────────────── */

struct analog_channel_state {
	int     coupling;        /* indice in coupling_options[] */
	int     vdiv_idx;        /* indice in hp1660es_osc_vdivs[] */
	float   vertical_offset; /* offset verticale in V */
	float   waveform_range;  /* range totale in V (RANGe SCPI) */
	float   waveform_offset; /* offset waveform in V */
	int     probe_factor;    /* 1 o 10 */
	gboolean state;          /* canale abilitato */

	/* Dati preamble OSC (da WAVeform:PREamble? dopo DATA?) */
	float   yincrement;      /* V/LSB (include probe factor) */
	float   yorigin;         /* V origine */
	float   yreference;      /* raw ADC zero (tipicamente 64) */
	float   xincrement;      /* s/campione */
	float   xorigin;         /* s origine */
	float   xreference;      /* campione zero */
	uint32_t num_samples;    /* campioni acquisiti (tipicamente 32768) */
};

/* ─────────────────────────────────────────────────────────────────────────────
 * struct pod_state
 * Stato dinamico di un pod LA.
 * ──────────────────────────────────────────────────────────────────────────── */

struct pod_state {
	float    threshold_v;      /* soglia in V */
	gboolean threshold_preset; /* TRUE se preset (TTL/ECL/CMOS) */
	int      threshold_idx;    /* 0=TTL, 1=ECL, 2=CMOS, -1=custom */
	gboolean enabled;          /* pod abilitato */
};

/* ─────────────────────────────────────────────────────────────────────────────
 * struct scope_config
 * Configurazione statica del modello — costruita una volta in device_init().
 * Speculare a ScopeConfig in hp1660_device.py.
 * Costante dopo l'inizializzazione.
 * ──────────────────────────────────────────────────────────────────────────── */

struct scope_config {
	/* Model info */
	char model_id[MAX_MODEL_ID_LEN];    /* es. "1660E" (da IDN) */
	char model_name[32];                /* es. "HP 1660ES" */
	char firmware_ver[32];              /* es. "REV 02.02" */

	/* Conteggi canali */
	uint8_t  digital_channels;  /* sempre 128 per HP 1660ES */
	uint8_t  analog_channels;   /* sempre 2 */
	uint8_t  pods;               /* sempre 8 */

	/* Nomi canali digitali: "A1.0".."A8.15" (128 stringhe) */
	const char *(*digital_names)[];

	/* Nomi pod: "POD_A1".."POD_A8" (8 stringhe) */
	const char *(*pod_names)[];

	/* Nomi canali analogici: "OSC1", "OSC2" */
	const char *(*analog_names)[];

	/* Opzioni coupling OSC */
	const char *(*coupling_options)[];
	uint8_t num_coupling_options;

	/* Sorgenti trigger OSC */
	const char *(*trigger_sources)[];
	uint8_t num_trigger_sources;

	/* Divisioni schermo */
	uint8_t num_xdivs;  /* 10 */
	uint8_t num_ydivs;  /* 4 divisioni — dal manuale:
	                     * ":CHANnel:RANGe defines the full-scale (4 × Volts/Div)"
	                     * RANGe SCPI = V/div × 4.
	                     * yincrement = RANGe / 256 (calcolato dallo strumento). */
};

/* ─────────────────────────────────────────────────────────────────────────────
 * struct scope_state
 * Stato dinamico corrente — aggiornato da scope_state_query().
 * Speculare a ScopeState in hp1660_device.py.
 * ──────────────────────────────────────────────────────────────────────────── */

struct scope_state {
	/* Stato canali analogici OSC (2 elementi) */
	struct analog_channel_state *analog_states;

	/* Stato canali digitali LA (array di gboolean, 128 elementi) */
	gboolean *digital_states;

	/* Stato pod LA (8 elementi) */
	struct pod_state *pod_states;

	/* ── LA ── */
	char     la_machine_type[16];    /* "TIMING" o "STATE" */
	char     la_acqmode[32];         /* "CONVENTIONAL, FULL" o "CONVENTIONAL, HALF" */
	uint64_t la_sample_period_ps;    /* periodo in picosecondi */
	uint64_t la_samplerate_hz;       /* Hz calcolato da period */
	int      la_samplerate_idx;      /* indice in hp1660es_la_samplerates[] */
	uint8_t  la_n_pod_pairs;         /* dal preamble blob (quanti pod attivi) */
	uint32_t la_samples_per_frame;   /* campioni per acquisizione */

	/* ── LA STATE mode — clock source (dal manuale: SFORmat subsystem) ──
	 *
	 * In STATE mode il campionamento è sincrono al clock del sistema
	 * sotto test (non c'è SPERIOD). La sequenza di configurazione usa
	 * SFORMAT invece di TFORMAT, e richiede:
	 *   :MACHINE1:SFORMAT:MASTER <clock_id>,<clock_spec>
	 *   :MACHINE1:SFORMAT:CLOCK<N> MASTER   (per ogni pod)
	 *
	 * clock_id  : {J|K|L|M|N|P}  — clock fisico dell'HP 1660E
	 *             J/K = pod pair 1/2, L/M = pod pair 3/4, N/P = pod pair 7/8
	 * clock_spec: {RISing|FALLing|BOTH|OFF}
	 * pod_clock : {MASTer|SLAVe|DEMultiplex}  — default MASTer
	 * sethold   : 0..9 (vedi Table 15-2 del manuale, default 0 = 3.5/0.0 ns)
	 * ──────────────────────────────────────────────────────────────────── */
	char     la_state_clock_id[4];    /* "J","K","L","M","N","P" — default "J" */
	char     la_state_clock_spec[8];  /* "RISing","FALLing","BOTH" — default "RISing" */
	int      la_state_sethold;        /* 0..9 — default 0 (3.5ns setup / 0.0ns hold) */

	/* ── Arming — cross-trigger intermodule ──
	 *
	 * Usato per acquisizioni miste LA+OSC (futura implementazione).
	 * :MACHINE1:ARM {RUN|MACHINE2|INTermodule}
	 * Default: "RUN" (acquisizione indipendente).
	 * ────────────────────────────────────────────────────────────────── */
	char     la_arm_source[16];       /* "RUN", "MACHINE2", "INTermodule" */

	/* ── OSC ── */
	int      osc_timebase_idx;       /* indice in hp1660es_osc_timebases[] */
	float    osc_timebase_delay_s;   /* TIMebase:DELay in secondi */
	char     osc_timebase_mode[16];  /* "AUTO" (read-only) */
	char     osc_acq_type[16];       /* "NORMal" o "AVERage" */
	int      osc_acq_count;          /* ACQuire:COUNt (solo se AVERage) */
	char     osc_trigger_source[16]; /* "CHANnel1", "CHANnel2", "EXTernal", "LINE" */
	char     osc_trigger_slope[16];  /* "POSitive" o "NEGative" */
	float    osc_trigger_level_v;    /* livello trigger in V */
	float    osc_horiz_triggerpos;   /* 0.0..1.0, 0.5=centro */

	/* ── Comune ── */
	uint64_t sample_rate;            /* Hz — valido dopo acquisizione */
};

/* ─────────────────────────────────────────────────────────────────────────────
 * enum acq_state
 * Macchina a stati per l'acquisizione asincrona (LA e OSC).
 * Sostituisce il booleano data_pending — permette di cedere il controllo
 * al loop degli eventi GLib tra una fase e l'altra senza bloccare la GUI.
 *
 * LA:
 *   IDLE → WAIT_ACQ (dopo :START)
 *   WAIT_ACQ → RECEIVING (quando *OPC? risponde 1 — trigger naturale)
 *   RECEIVING → IDLE (blob completo, dati processati)
 *
 *   Nota: ACQ_STATE_WAIT_STOP non viene più usato per LA (fix v16).
 *   Il :STOP forzato impediva a SYSTEM:DATA? di restituire dati freschi.
 *
 * OSC:
 *   IDLE → OSC_WAIT_DIGITIZE (dopo :DIGitize)
 *   OSC_WAIT_DIGITIZE → OSC_RECEIVING (dopo attesa calibrata sul timebase)
 *   OSC_RECEIVING → IDLE (blob completo, dati processati)
 * ──────────────────────────────────────────────────────────────────────────── */

enum acq_type {
	ACQ_TYPE_LA,
	ACQ_TYPE_OSC,
	ACQ_TYPE_MIXED,
};

enum acq_state {
	ACQ_STATE_IDLE,
	ACQ_STATE_WAIT_ACQ,
	ACQ_STATE_WAIT_STOP,
	ACQ_STATE_RECEIVING,
	ACQ_STATE_OSC_WAIT_DIGITIZE,
	ACQ_STATE_OSC_RECEIVING,
	ACQ_STATE_MIXED_WAIT_ACQ,
	ACQ_STATE_MIXED_RECEIVING_LA,
	ACQ_STATE_MIXED_OSC_WAIT_DIGITIZE,
	ACQ_STATE_MIXED_RECEIVING_OSC,
};

/* ─────────────────────────────────────────────────────────────────────────────
 * struct dev_context
 * Contesto runtime del driver — uno per device instance.
 * Speculare a DeviceContext in hp1660_device.py e struct dev_context del DLM.
 * ──────────────────────────────────────────────────────────────────────────── */

struct dev_context {
	const struct scope_config *model_config;  /* puntatore alla config statica */
	struct scope_state        *model_state;   /* stato dinamico allocato */

	/* Channel groups per PulseView */
	struct sr_channel_group **analog_groups;   /* 2 gruppi (OSC1, OSC2) */
	struct sr_channel_group **digital_groups;  /* 8 gruppi (POD_A1..POD_A8) */

	/* Acquisizione */
	GSList   *enabled_channels;   /* canali abilitati per questa acquisizione */
	GSList   *current_channel;    /* canale corrente (iterazione) */
	uint64_t  num_frames;         /* frame acquisiti finora */
	uint64_t  frame_limit;        /* limite frame (0 = illimitato) */

	/*
	 * Tipo acquisizione corrente — determinato in dev_acquisition_start()
	 * in base ai canali abilitati.
	 *
	 * ACQ_TYPE_LA    → solo canali logici  (SELECT 1, SYSTEM:DATA?)
	 * ACQ_TYPE_OSC   → solo canali analogici (SELECT 2, WAVeform:DATA?)
	 * ACQ_TYPE_MIXED → LA+OSC cross-armed via INTermodule
	 *                  (LA in Group Run, OSC in IMMEDIATE, INSert tree)
	 */
	enum acq_type acq_type;

	/*
	 * Macchina a stati asincrona.
	 * Sostituisce data_pending — permette al callback di cedere il controllo
	 * al loop GLib tra una fase e l'altra senza g_usleep bloccanti.
	 */
	enum acq_state acq_state;      /* stato corrente acquisizione */
	gint64         acq_timer_start; /* timestamp GLib per delay non bloccanti */

	/*
	 * Buffer di ricezione blob — usato sia per LA (SYSTEM:DATA?)
	 * che per OSC (WAVeform:DATA?).
	 * Allocato con dimensione iniziale conservativa, poi riallocato
	 * dinamicamente (g_realloc) appena l'header IEEE 488.2 rivela
	 * la dimensione esatta del payload. Azzerato dopo ogni blob completo.
	 */
	uint8_t  *blob_buf;        /* buffer grezzo del blob */
	size_t    blob_len;        /* byte ricevuti finora */
	size_t    blob_expected;   /* byte attesi totali (hdr + payload) */
	size_t    blob_allocated;  /* byte attualmente allocati in blob_buf */
	int       mixed_osc_extra_byte; /* 1 se il blob OSC ha 1 byte extra (\n) in mixed mode */
	char     *mixed_osc_preamble;   /* preamble OSC letto prima di WAVeform:DATA? in mixed mode */
};

/* ─────────────────────────────────────────────────────────────────────────────
 * Funzioni esportate — protocol.c
 * ──────────────────────────────────────────────────────────────────────────── */

/* Inizializzazione e probe */
SR_PRIV int  hp1660es_model_match(const char *idn_model,
		char *model_name_out, size_t name_len);
SR_PRIV int  hp1660es_device_init(struct sr_dev_inst *sdi);
SR_PRIV void hp1660es_scope_state_destroy(struct scope_state *state);

/* State query */
SR_PRIV int  hp1660es_scope_state_query(struct sr_dev_inst *sdi);

/* Channel management */
SR_PRIV int  hp1660es_channel_state_set(const struct sr_dev_inst *sdi,
		int ch_index, gboolean state);
SR_PRIV int  hp1660es_check_channels(GSList *channels);

/* Acquisizione LA */
SR_PRIV int  hp1660es_la_configure(const struct sr_dev_inst *sdi);
SR_PRIV int  hp1660es_la_acquire(const struct sr_dev_inst *sdi);
SR_PRIV int  hp1660es_la_data_receive(int fd, int revents, void *cb_data);

/* Acquisizione OSC */
SR_PRIV int hp1660es_osc_configure(const struct sr_dev_inst *sdi);
SR_PRIV int hp1660es_osc_acquire(const struct sr_dev_inst *sdi);
SR_PRIV int hp1660es_osc_data_receive(int fd, int revents, void *cb_data);

/* Acquisizione mixed LA+OSC (cross-arming) */
SR_PRIV int hp1660es_mixed_configure(const struct sr_dev_inst *sdi);
SR_PRIV int hp1660es_mixed_acquire(const struct sr_dev_inst *sdi);
SR_PRIV int hp1660es_mixed_data_receive(int fd, int revents, void *cb_data);

/* Helpers blob parsing */
SR_PRIV int  hp1660es_blob_find_osc_section(const uint8_t *blob, size_t blob_len,
		const uint8_t **section_out, size_t *section_len_out,
		size_t *waveform_offset_out);
SR_PRIV int  hp1660es_blob_find_la_section(const uint8_t *blob, size_t blob_len,
		const uint8_t **section_out, size_t *section_len_out);
SR_PRIV int  hp1660es_la_parse_preamble(const uint8_t *preamble,
		uint64_t *sample_period_ps_out,
		uint8_t  *n_pod_pairs_out,
		uint32_t *valid_rows_out);
SR_PRIV int  hp1660es_la_samples_send(const uint8_t *data, size_t data_len,
		uint32_t n_rows, struct sr_dev_inst *sdi);

/* Helpers OSC */
SR_PRIV int  hp1660es_osc_preamble_parse(const char *preamble_str,
		struct analog_channel_state *ach);
SR_PRIV int  hp1660es_osc_samples_send(const uint8_t *data, uint32_t n_samples,
		const struct analog_channel_state *ach,
		struct sr_dev_inst *sdi);

/* Utility */
SR_PRIV void hp1660es_scope_state_dump(const struct scope_config *config,
		const struct scope_state *state);
SR_PRIV int  hp1660es_drain_errors(struct sr_scpi_dev_inst *scpi);

#endif /* LIBSIGROK_HARDWARE_HP1660ES_PROTOCOL_H */
