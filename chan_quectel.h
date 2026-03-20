/*
   Copyright (C) 2009-2015

   bg <bg_one@mail.ru>
   http://www.e1550.mobi

   Artem Makhutov <artem@makhutov.org>
   http://www.makhutov.org

   Dmitry Vagin <dmitry2004@yandex.ru>
*/
#ifndef CHAN_QUECTEL_H_INCLUDED
#define CHAN_QUECTEL_H_INCLUDED

#include "ast_config.h"

#include <asterisk/lock.h>
#include <asterisk/linkedlists.h>

#include "ast_compat.h"				/* asterisk compatibility fixes */

#include "mixbuffer.h"				/* struct mixbuffer */
#include "ringbuffer.h"				/* struct ringbuffer */
#include "cpvt.h"				/* struct cpvt */
#include "export.h"				/* EXPORT_DECL EXPORT_DEF */
#include "dc_config.h"				/* pvt_config_t */
#include "at_command.h"

#include <alsa/asoundlib.h>
#define PERIOD_FRAMES           80
#define DESIRED_RATE 8000
#define ALSA_PCM_NEW_HW_PARAMS_API
#define ALSA_PCM_NEW_SW_PARAMS_API
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#if __BYTE_ORDER == __LITTLE_ENDIAN
static snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;
#else
static snd_pcm_format_t format = SND_PCM_FORMAT_S16_BE;
#endif
static int silencesuppression = 0;
static int silencethreshold = 1000;
#define MAX_BUFFER_SIZE 100

static int writedev = -1;

#define MODULE_DESCRIPTION	"Channel Driver for Mobile Telephony"
#define MAXQUECTELDEVICES	128

INLINE_DECL const char * dev_state2str(dev_state_t state)
{
	return enum2str(state, dev_state_strs, ITEMS_OF(dev_state_strs));
}

INLINE_DECL const char * dev_state2str_msg(dev_state_t state)
{
	static const char * const states[] = { "Stop scheduled", "Restart scheduled", "Removal scheduled", "Start scheduled" };
	return enum2str(state, states, ITEMS_OF(states));
}

#if ASTERISK_VERSION_NUM >= 100000 && ASTERISK_VERSION_NUM < 130000 /* 10-13 */
/* Only linear is allowed */
EXPORT_DECL struct ast_format chan_quectel_format;
EXPORT_DECL struct ast_format_cap * chan_quectel_format_cap;
#endif /* ^10-13 */

typedef enum {
	RESTATE_TIME_NOW	= 0,
	RESTATE_TIME_GRACEFULLY,
	RESTATE_TIME_CONVENIENT,
} restate_time_t;

/* state */
typedef struct pvt_state
{
	char			audio_tty[DEVPATHLEN];		/*!< tty for audio connection */
	char			data_tty[DEVPATHLEN];		/*!< tty for AT commands */

	uint32_t		at_tasks;			/*!< number of active tasks in at_queue */
	uint32_t		at_cmds;			/*!< number of active commands in at_queue */
	uint32_t		chansno;			/*!< number of channels in channels list */
	uint8_t			chan_count[CALL_STATES_NUMBER];	/*!< channel number grouped by state */
} pvt_state_t;

#define PVT_STATE_T(state, name)			((state)->name)

/* statictics */
typedef struct pvt_stat
{
	uint32_t		at_tasks;			/*!< number of tasks added to queue */
	uint32_t		at_cmds;			/*!< number of commands added to queue */
	uint32_t		at_responses;			/*!< number of responses handled */

	uint32_t		d_read_bytes;			/*!< number of bytes of commands actually read from device */
	uint32_t		d_write_bytes;			/*!< number of bytes of commands actually written to device */

	uint64_t		a_read_bytes;			/*!< number of bytes of audio read from device */
	uint64_t		a_write_bytes;			/*!< number of bytes of audio written to device */

	uint32_t		read_frames;			/*!< number of frames read from device */
	uint32_t		read_sframes;			/*!< number of truncated frames read from device */

	uint32_t		write_frames;			/*!< number of tries to frame write */
	uint32_t		write_tframes;			/*!< number of truncated frames to write */
	uint32_t		write_sframes;			/*!< number of silence frames to write */

	uint64_t		write_rb_overflow_bytes;	/*!< number of overflow bytes */
	uint32_t		write_rb_overflow;		/*!< number of times when a_write_rb overflowed */

	uint32_t		in_calls;			/*!< number of incoming calls not including waiting */
	uint32_t		cw_calls;			/*!< number of waiting calls */
	uint32_t		out_calls;			/*!< number of all outgoing calls attempts */
	uint32_t		in_calls_handled;		/*!< number of ncoming/waiting calls passed to dialplan */
	uint32_t		in_pbx_fails;			/*!< number of start_pbx fails */

	uint32_t		calls_answered[2];		/*!< number of outgoing and incoming/waiting calls answered */
	uint32_t		calls_duration[2];		/*!< seconds of outgoing and incoming/waiting calls */
} pvt_stat_t;

#define PVT_STAT_T(stat, name)			((stat)->name)

struct at_queue_task;

typedef unsigned int sms_inbox_item_type;

#define SMS_INBOX_ITEM_BITS     (sizeof(sms_inbox_item_type) * 8)
#define SMS_INBOX_ARRAY_SIZE    ((SMS_INDEX_MAX + SMS_INBOX_ITEM_BITS - 1) / SMS_INBOX_ITEM_BITS)

typedef struct pvt
{
	AST_LIST_ENTRY (pvt)	entry;				/*!< linked list pointers */

	ast_mutex_t		lock;				/*!< pvt lock */
	AST_LIST_HEAD_NOLOCK (, at_queue_task) at_queue;	/*!< queue for commands to modem */

	AST_LIST_HEAD_NOLOCK (, cpvt)		chans;		/*!< list of channels */
	struct cpvt		sys_chan;			/*!< system channel */
	struct cpvt		*last_dialed_cpvt;		/*!< channel what last call successfully set ATDnum; leave until ^ORIG received; need because real call idx of dialing call unknown until ^ORIG */

	pthread_t		monitor_thread;			/*!< monitor (at commands reader) thread handle */

        snd_pcm_t               *icard, *ocard;
	int			audio_fd;			/*!< audio descriptor */
	int			data_fd;			/*!< data descriptor */
	char			* alock;			/*!< name of lockfile for audio */
	char			* dlock;			/*!< name of lockfile for data */

	struct ast_dsp*		dsp;				/*!< silence/DTMF detector - FIXME: must be in cpvt */
	dc_dtmf_setting_t	real_dtmf;			/*!< real DTMF setting */

	struct ast_timer*	a_timer;			/*!< audio write timer */

	char			a_write_buf[FRAME_SIZE * 5];	/*!< audio write buffer */
	size_t			uac_write_len;			/*!< pending UAC playback bytes in a_write_buf */
	int				uac_readpos;			/*!< accumulated UAC capture samples */
	int				uac_readleft;			/*!< remaining UAC capture samples for a full frame */
	char			uac_rx_assem_buf[FRAME_SIZE];	/*!< temporary UAC capture frame assembly buffer */
	char			uac_tx_buf[FRAME_SIZE * 6];	/*!< adaptive UAC uplink queue storage */
	struct ringbuffer	uac_tx_rb;			/*!< adaptive UAC uplink queue */
	char			uac_rx_buf[FRAME_SIZE * 6];	/*!< adaptive UAC downlink queue storage */
	struct ringbuffer	uac_rx_rb;			/*!< adaptive UAC downlink queue */
	char			uac_last_tx_frame[FRAME_SIZE];	/*!< last transmitted UAC uplink frame for PLC */
	char			uac_last_rx_frame[FRAME_SIZE];	/*!< last captured UAC downlink frame for PLC */
	unsigned int	uac_have_last_tx:1;		/*!< uplink PLC history available */
	unsigned int	uac_have_last_rx:1;		/*!< downlink PLC history available */
	unsigned int	uac_tx_plc_left:2;		/*!< remaining uplink PLC frames */
	unsigned int	uac_rx_plc_left:2;		/*!< remaining downlink PLC frames */
	unsigned int	uac_rx_fadein_left:2;		/*!< fade-in frames after RX gap/recovery */
	unsigned int	uac_target_frames:3;		/*!< adaptive target buffering depth, in 20ms frames */
	unsigned int	uac_stable_ticks;		/*!< consecutive stable 20ms ticks */
	unsigned int	uac_decay_hold_ticks;		/*!< short post-distress decay hold window, in 20ms ticks */
	unsigned int	uac_target_ceiling_boost:2;	/*!< temporary TX-stress ceiling boost above base target ceiling */
	unsigned int	uac_diag_dtmf_begin;		/*!< DTMF begin events in current UAC call */
	unsigned int	uac_diag_dtmf_drop_pending;	/*!< pending queued DTMF dropped on hangup */
	unsigned int	uac_diag_dtmf_send_ok;		/*!< modem acknowledged DTMF send */
	unsigned int	uac_diag_dtmf_send_ok_sys;	/*!< DTMF send success happened on sys/non-call cpvt */
	unsigned int	uac_diag_dtmf_send_ok_late;	/*!< DTMF send success completed after call channel was gone */
	unsigned int	uac_diag_dtmf_send_err;		/*!< modem rejected/failed DTMF send */
	unsigned int	uac_diag_dtmf_send_err_sys;	/*!< DTMF send error happened on sys/non-call cpvt */
	unsigned int	uac_diag_dtmf_send_err_late;	/*!< DTMF send error completed after call channel was gone */
	unsigned int	uac_diag_tx_rb_drop;		/*!< TX ringbuffer dropped an old frame */
	unsigned int	uac_diag_rx_rb_drop;		/*!< RX ringbuffer dropped an old frame */
	unsigned int	uac_diag_capture_prepare;	/*!< capture prepare attempts */
	unsigned int	uac_diag_capture_start;		/*!< capture start attempts */
	unsigned int	uac_diag_capture_avail_recover;	/*!< capture avail recover attempts */
	unsigned int	uac_diag_capture_read_recover;	/*!< capture read recover attempts */
	unsigned int	uac_diag_playback_prepare;	/*!< playback prepare attempts */
	unsigned int	uac_diag_playback_avail_recover;	/*!< playback avail recover attempts */
	unsigned int	uac_diag_playback_write_recover;	/*!< playback write recover attempts */
	unsigned int	uac_diag_runtime_reopen;	/*!< runtime-triggered UAC sound card reopen attempts */
	unsigned int	uac_capture_bad_ticks;	/*!< consecutive capture-side ALSA faults/recoveries */
	unsigned int	uac_playback_bad_ticks;	/*!< consecutive playback-side ALSA faults/recoveries */
	unsigned int	uac_diag_capture_degraded_ticks;	/*!< capture tick reported degraded */
	unsigned int	uac_diag_playback_degraded_ticks;	/*!< playback tick reported degraded */
	unsigned int	uac_diag_playback_plc_ticks;	/*!< playback tick used short PLC frame because TX queue was empty */
	unsigned int	uac_diag_playback_hold_ticks;	/*!< playback tick used extended DTMF hold frame because TX queue was empty */
	unsigned int	uac_diag_playback_silence_ticks;	/*!< playback tick injected silence because TX queue was empty */
	unsigned int	uac_diag_playback_short_write_breaks;	/*!< playback loop stopped with unwritten samples left */
	unsigned int	uac_diag_playback_degraded_logs;	/*!< playback degraded detail logs already emitted */
	unsigned int	uac_diag_tx_frame_in;		/*!< TX voice frames queued into UAC */
	unsigned int	uac_diag_tx_gap_25ms;		/*!< gap >25ms between queued TX voice frames */
	unsigned int	uac_diag_tx_gap_60ms;		/*!< gap >60ms between queued TX voice frames */
	unsigned int	uac_diag_tx_gap_250ms;		/*!< gap >250ms between queued TX voice frames */
	unsigned int	uac_diag_tx_gap_60ms_post_answer;	/*!< TX gaps >60ms after answer */
	unsigned int	uac_diag_tx_gap_60ms_dtmf;	/*!< TX gaps >60ms during DTMF window */
	unsigned int	uac_diag_tx_gap_max_ms;		/*!< maximum observed gap between queued TX voice frames */
	unsigned int	uac_diag_tx_rb_empty_after_tx;	/*!< playback found TX rb empty after TX had already started */
	unsigned int	uac_diag_tx_rb_empty_gap_60ms;	/*!< TX rb empty after TX start with last gap >60ms */
	unsigned int	uac_diag_playback_degraded_after_tx;	/*!< degraded playback ticks after first TX frame was seen */
	unsigned int	uac_diag_playback_plc_after_tx;	/*!< PLC playback ticks after first TX frame was seen */
	unsigned int	uac_diag_playback_hold_after_tx;	/*!< extended DTMF hold playback ticks after first TX frame was seen */
	unsigned int	uac_diag_playback_silence_after_tx;	/*!< silence playback ticks after first TX frame was seen */
	unsigned int	uac_diag_playback_degraded_dtmf;	/*!< degraded playback ticks during DTMF window */
	unsigned int	uac_diag_playback_plc_dtmf;	/*!< PLC playback ticks during DTMF window */
	unsigned int	uac_diag_playback_hold_dtmf;	/*!< extended DTMF hold playback ticks during DTMF window */
	unsigned int	uac_diag_playback_silence_dtmf;	/*!< silence playback ticks during DTMF window */
	unsigned int	uac_diag_rx_frame_in;		/*!< RX voice frames queued from UAC capture */
	unsigned int	uac_diag_rx_gap_25ms;		/*!< gap >25ms between queued RX voice frames */
	unsigned int	uac_diag_rx_gap_60ms;		/*!< gap >60ms between queued RX voice frames */
	unsigned int	uac_diag_rx_gap_250ms;		/*!< gap >250ms between queued RX voice frames */
	unsigned int	uac_diag_rx_gap_60ms_post_answer;	/*!< RX gaps >60ms after answer */
	unsigned int	uac_diag_rx_gap_60ms_dtmf;	/*!< RX gaps >60ms during DTMF window */
	unsigned int	uac_diag_rx_gap_max_ms;		/*!< maximum observed gap between queued RX voice frames */
	unsigned int	uac_diag_rx_empty_after_rx;	/*!< dequeue found RX rb empty after RX had started */
	unsigned int	uac_diag_rx_empty_gap_60ms;	/*!< RX rb empty after RX start with last gap >60ms */
	unsigned int	uac_diag_rx_plc_ticks;		/*!< RX dequeue used PLC frame */
	unsigned int	uac_diag_rx_silence_ticks;		/*!< RX dequeue injected silence */
	unsigned int	uac_diag_rx_plc_post_answer;	/*!< RX PLC after answer */
	unsigned int	uac_diag_rx_silence_post_answer;	/*!< RX silence after answer */
	unsigned int	uac_diag_rx_out_frame_out;	/*!< RX voice frames delivered out of channel_read() */
	unsigned int	uac_diag_rx_out_gap_25ms;	/*!< gap >25ms between delivered RX voice frames */
	unsigned int	uac_diag_rx_out_gap_60ms;	/*!< gap >60ms between delivered RX voice frames */
	unsigned int	uac_diag_rx_out_gap_250ms;	/*!< gap >250ms between delivered RX voice frames */
	unsigned int	uac_diag_rx_out_gap_60ms_post_answer;	/*!< delivered RX gaps >60ms after answer */
	unsigned int	uac_diag_rx_out_gap_60ms_dtmf;	/*!< delivered RX gaps >60ms during DTMF window */
	unsigned int	uac_diag_rx_out_gap_max_ms;	/*!< maximum observed gap between delivered RX voice frames */
	unsigned int	uac_diag_rx_out_degraded;	/*!< delivered RX voice frames marked degraded */
	unsigned int	uac_diag_rx_out_plc;	/*!< delivered RX voice frames that came from PLC */
	unsigned int	uac_diag_rx_out_silence;	/*!< delivered RX voice frames that were injected silence */
	unsigned int	uac_diag_playback_last_state;	/*!< last observed ALSA playback state when degraded */
	snd_pcm_sframes_t	uac_diag_playback_last_avail;	/*!< last observed ALSA playback avail when degraded */
	snd_pcm_uframes_t	uac_diag_playback_last_queued;	/*!< last observed queued playback frames when degraded */
	snd_pcm_uframes_t	uac_diag_playback_last_want_fill;	/*!< last observed playback target fill when degraded */
	unsigned int	uac_diag_playback_last_tx_rb_frames;	/*!< last observed queued TX frames when degraded */
	snd_pcm_uframes_t	uac_diag_playback_last_left;	/*!< last unwritten playback frames when degraded */
	unsigned int	uac_diag_playback_last_gap_ms;	/*!< ms since last queued TX frame when degraded */
	unsigned int	uac_diag_tx_started;		/*!< first queued TX frame has been seen in this call */
	unsigned int	uac_diag_rx_started;		/*!< first queued RX frame has been seen in this call */
	unsigned int	uac_diag_rx_out_started;		/*!< first delivered RX frame has been seen in this call */
	unsigned int	uac_diag_answered;		/*!< answer marker for current UAC call */
	unsigned int	uac_diag_summary_logged;		/*!< summary already emitted for current UAC call */
	struct timeval	uac_diag_call_start;		/*!< timestamp when current UAC runtime started */
	struct timeval	uac_diag_answer_time;	/*!< timestamp when call was marked answered */
	struct timeval	uac_diag_last_dtmf_begin;	/*!< timestamp of last DTMF begin */
	struct timeval	uac_diag_last_tx_enqueue;	/*!< timestamp of last queued TX frame */
	struct timeval	uac_diag_last_rx_enqueue;	/*!< timestamp of last queued RX frame */
	struct timeval	uac_diag_last_rx_delivery;	/*!< timestamp of last delivered RX voice frame */
	snd_pcm_uframes_t	uac_capture_period;		/*!< negotiated UAC capture period size */
	snd_pcm_uframes_t	uac_capture_buffer;		/*!< negotiated UAC capture buffer size */
	snd_pcm_uframes_t	uac_playback_period;		/*!< negotiated UAC playback period size */
	snd_pcm_uframes_t	uac_playback_buffer;		/*!< negotiated UAC playback buffer size */
	struct mixbuffer	a_write_mixb;			/*!< audio mix buffer */
//	struct ringbuffer	a_write_rb;			/*!< audio ring buffer */

//	char			a_read_buf[FRAME_SIZE + AST_FRIENDLY_OFFSET];	/*!< audio read buffer */
//	struct ast_frame	a_read_frame;			/*!< read frame buffer */


	unsigned int		user_cmd_sync_seq;		/*!< monotonically increasing sequence for synchronous CLI AT requests */
	unsigned int		user_cmd_sync_pending_seq;	/*!< currently pending synchronous CLI AT request sequence */
	unsigned int		user_cmd_sync_done_seq;	/*!< last completed synchronous CLI AT request sequence */
	char			*user_cmd_sync_response;	/*!< cached response text for last synchronous CLI AT request */
	unsigned int		user_cmd_sync_response_truncated:1;	/*!< synchronous CLI AT response was truncated */

	char			dtmf_digit;			/*!< last DTMF digit */
	struct timeval		dtmf_begin_time;		/*!< time of begin of last DTMF digit */
	struct timeval		dtmf_end_time;			/*!< time of end of last DTMF digit */

	int			timeout;			/*!< used to set the timeout for data */
#define DATA_READ_TIMEOUT	10000				/* 10 seconds */

	unsigned long		channel_instance;		/*!< number of channels created on this device */
	unsigned int		rings;				/*!< ring/ccwa  number distributed to at_response_clcc() */

	/* device caps */
	unsigned int		use_ucs2_encoding:1;

	/* device state */
	int			gsm_reg_status;
	int			rssi;
	int			linkmode;
	int			linksubmode;
	char			provider_name[32];
	char			manufacturer[32];
	char			model[32];
	char			firmware[32];
	char			imei[17];
	char			imsi[17];
	char			subscriber_number[128];
	char			location_area_code[8];
	char			cell_id[8];
	char			sms_scenter[20];

	unsigned int		incoming_sms_index;
	sms_inbox_item_type	incoming_sms_inbox[SMS_INBOX_ARRAY_SIZE];

	volatile unsigned int	connected:1;			/*!< do we have an connection to a device */
	unsigned int		initialized:1;			/*!< whether a service level connection exists or not */
	unsigned int		gsm_registered:1;		/*!< do we have an registration to a GSM */
	unsigned int		dialing;			/*!< HW state; true from ATD response OK until CEND or CONN for this call idx */
	unsigned int		ring:1;				/*!< HW state; true if has incoming call from first RING until CEND or CONN */
	unsigned int		cwaiting:1;			/*!< HW state; true if has incoming call waiting from first CCWA until CEND or CONN for */
	unsigned int		outgoing_sms:1;			/*!< outgoing sms */
	unsigned int		volume_sync_step:2;		/*!< volume synchronized stage */
#define VOLUME_SYNC_BEGIN	0
#define VOLUME_SYNC_DONE	3

	unsigned int		has_sms:1;			/*!< device has SMS support */
	unsigned int		has_voice:1;			/*!< device has voice call support */
	unsigned int		is_simcom:1;			/*!< device is a simcom module */
        long                    t0;
	unsigned int		call_estb:1;
	unsigned int		has_call_waiting:1;		/*!< call waiting enabled on device */

	unsigned int		group_last_used:1;		/*!< mark the last used device */
	unsigned int		prov_last_used:1;		/*!< mark the last used device */
	unsigned int		sim_last_used:1;		/*!< mark the last used device */

	unsigned int		terminate_monitor:1;		/*!< non-zero if we want terminate monitor thread i.e. restart, stop, remove */
//	unsigned int		off:1;				/*!< device not used */
//	unsigned int		prevent_new:1;			/*!< prevent new usage */

	unsigned int		has_subscriber_number:1;	/*!< subscriber_number field is valid */
//	unsigned int		monitor_running:1;		/*!< true if monitor thread is running */
	unsigned int		must_remove:1;			/*!< mean must removed from list: NOT FULLY THREADSAFE */

	volatile dev_state_t	desired_state;			/*!< desired state */
	volatile restate_time_t	restart_time;			/*!< time when change state */
	volatile dev_state_t	current_state;			/*!< current state */

	pvt_config_t		settings;			/*!< all device settings from config file */
	pvt_state_t		state;				/*!< state */
	pvt_stat_t		stat;				/*!< various statistics */
} pvt_t;

#define CONF_GLOBAL(name)		(gpublic->global_settings.name)
#define SCONF_GLOBAL(state, name)	((state)->global_settings.name)

#define CONF_SHARED(pvt, name)		SCONFIG(&((pvt)->settings), name)
#define CONF_UNIQ(pvt, name)		UCONFIG(&((pvt)->settings), name)
#define PVT_ID(pvt)			UCONFIG(&((pvt)->settings), id)

#define PVT_STATE(pvt, name)		PVT_STATE_T(&(pvt)->state, name)
#define PVT_STAT(pvt, name)		PVT_STAT_T(&(pvt)->stat, name)

typedef struct public_state
{
	AST_RWLIST_HEAD(devices, pvt)	devices;
	ast_mutex_t			discovery_lock;
	pthread_t			discovery_thread;		/* The discovery thread handler */
	volatile int			unloading_flag;			/* no need mutex or other locking for protect this variable because no concurent r/w and set non-0 atomically */
	struct dc_gconfig		global_settings;
} public_state_t;

EXPORT_DECL public_state_t * gpublic;

EXPORT_DEF int sms_inbox_set(struct pvt* pvt, int index);
EXPORT_DEF int sms_inbox_clear(struct pvt* pvt, int index);
EXPORT_DEF int is_sms_inbox_set(const struct pvt* pvt, int index);

EXPORT_DECL void clean_read_data(const char * devname, int fd);
EXPORT_DECL int pvt_get_pseudo_call_idx(const struct pvt * pvt);
EXPORT_DECL int ready4voice_call(const struct pvt* pvt, const struct cpvt * current_cpvt, int opts);
EXPORT_DECL int is_dial_possible(const struct pvt * pvt, int opts);

EXPORT_DECL const char * pvt_str_state(const struct pvt* pvt);
EXPORT_DECL struct ast_str * pvt_str_state_ex(const struct pvt* pvt);
EXPORT_DECL const char * GSM_regstate2str(int gsm_reg_status);
EXPORT_DECL const char * sys_mode2str(int sys_mode);
EXPORT_DECL const char * sys_submode2str(int sys_submode);
EXPORT_DECL char* rssi2dBm(int rssi, char* buf, unsigned len);

EXPORT_DECL void pvt_on_create_1st_channel(struct pvt* pvt);
EXPORT_DECL void pvt_on_remove_last_channel(struct pvt* pvt);
EXPORT_DECL void pvt_reload(restate_time_t when);
EXPORT_DECL int pvt_enabled(const struct pvt * pvt);
EXPORT_DECL void pvt_try_restate(struct pvt * pvt);

EXPORT_DECL int opentty (const char* dev, char ** lockfile, int typ);
EXPORT_DECL void closetty(int fd, char ** lockfname);
EXPORT_DECL int lock_try(const char * devname, char ** lockname);
EXPORT_DECL struct pvt * find_device_ex(struct public_state * state, const char * name);

INLINE_DECL struct pvt * find_device (const char* name)
{
	return find_device_ex(gpublic, name);
}

EXPORT_DECL struct pvt * find_device_ext(const char* name);
EXPORT_DECL struct pvt * find_device_by_resource_ex(struct public_state * state, const char * resource, int opts, const struct ast_channel * requestor, int * exists);
EXPORT_DECL void pvt_dsp_setup(struct pvt * pvt, const char * id, dc_dtmf_setting_t dtmf_new);

INLINE_DECL struct pvt * find_device_by_resource(const char * resource, int opts, const struct ast_channel * requestor, int * exists)
{
	return find_device_by_resource_ex(gpublic, resource, opts, requestor, exists);
}

EXPORT_DECL struct ast_module * self_module();

#define PVT_NO_CHANS(pvt)		(PVT_STATE(pvt, chansno) == 0)

#endif /* CHAN_QUECTEL_H_INCLUDED */
