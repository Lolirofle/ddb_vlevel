#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <deadbeef/deadbeef.h>
#include "volumeleveler.h"

enum{
	VLEVEL_PARAM_STRENGTH,
	VLEVEL_PARAM_MAX_MULTIPLIER,
	VLEVEL_PARAM_MAX_BUFFER_DURATION,
	VLEVEL_PARAM_COUNT
};

DB_functions_t *deadbeef;
DB_dsp_t plugin;

struct ddb_vlevel_t{
	ddb_dsp_context_t ctx;
	VolumeLeveler *vl;
	double buffer_duration; //Seconds. Range: Positive.
	int prev_sample_rate;

	size_t buffer_length(int sample_rate){
		return (size_t)(buffer_duration * sample_rate + 0.5);
	}
};


ddb_dsp_context_t *vlevel_open(void){
	ddb_vlevel_t *data = (ddb_vlevel_t *) malloc(sizeof(ddb_vlevel_t));
	DDB_INIT_DSP_CONTEXT(data,ddb_vlevel_t,&plugin);

	//Initialise.
	data->buffer_duration = 2.0;
	data->prev_sample_rate = 44100;
	data->vl = new VolumeLeveler();
	data->vl->SetStrength(0.8);
	data->vl->SetMaxMultiplier(25);

	return(ddb_dsp_context_t *)data;
}

void vlevel_close(ddb_dsp_context_t *ctx){
	ddb_vlevel_t *data = (ddb_vlevel_t *)ctx;
	free(data);
}

void vlevel_reset(ddb_dsp_context_t *ctx){
	ddb_vlevel_t *data = (ddb_vlevel_t *)ctx;
	data->vl->Flush();
}

//TODO: There is a delay due to the buffer length. The duration indicator after seeking becomes incorrect. Songs also stop early (last part of buffer gets deleted before it has ended). Is there a way to fix this?
int vlevel_process(ddb_dsp_context_t *ctx, float *samples, int nframes, int maxframes, ddb_waveformat_t *fmt, float *r){
	ddb_vlevel_t *data = (ddb_vlevel_t *)ctx;

	if(data->prev_sample_rate != fmt->samplerate || (int)data->vl->GetChannels() != fmt->channels){
		data->prev_sample_rate = fmt->samplerate;
		data->vl->SetSamplesAndChannels(data->buffer_length(fmt->samplerate),data->vl->GetChannels());
	}

	return nframes - data->vl->Exchange<value_t *,bufferExchangeInterleavedIndex>(samples,samples,(size_t)nframes);
}

const char *vlevel_get_param_name(int p){
	switch(p){
	case VLEVEL_PARAM_STRENGTH           : return "Strength";
	case VLEVEL_PARAM_MAX_MULTIPLIER     : return "Max multiplier";
	case VLEVEL_PARAM_MAX_BUFFER_DURATION: return "Buffer duration";
	default: fprintf(stderr, "vlevel_param_name: invalid param index (%d)\n", p);
	}
	return NULL;
}

int vlevel_num_params(void){
	return VLEVEL_PARAM_COUNT;
}

void vlevel_set_param(ddb_dsp_context_t *ctx, int p, const char *val){
	ddb_vlevel_t *data = (ddb_vlevel_t *)ctx;
	switch(p){
	case VLEVEL_PARAM_STRENGTH:
		data->vl->SetStrength(atof(val));
		break;
	case VLEVEL_PARAM_MAX_MULTIPLIER:
		data->vl->SetMaxMultiplier(atof(val));
		break;
	case VLEVEL_PARAM_MAX_BUFFER_DURATION:
		data->buffer_duration = atof(val);
		data->vl->SetSamplesAndChannels(data->buffer_length(data->prev_sample_rate),data->vl->GetChannels());
		break;
	default:
		fprintf(stderr, "vlevel_param: invalid param index (%d)\n", p);
	}
}

void vlevel_get_param(ddb_dsp_context_t *ctx, int p, char *val, int sz){
	ddb_vlevel_t *data = (ddb_vlevel_t *)ctx;
	switch(p){
	case VLEVEL_PARAM_STRENGTH           : snprintf(val, sz, "%f", data->vl->GetStrength());      break;
	case VLEVEL_PARAM_MAX_MULTIPLIER     : snprintf(val, sz, "%f", data->vl->GetMaxMultiplier()); break;
	case VLEVEL_PARAM_MAX_BUFFER_DURATION: snprintf(val, sz, "%f", data->buffer_duration);        break;
	default:
		fprintf(stderr, "vlevel_get_param: invalid param index (%d)\n", p);
	}
}

const char settings_dlg[] =
	"property \"Strength\" spinbtn[0.00,1.00,0.01] 0 0.8;\n"
	"property \"Max multiplier (20*log10(m) dB)\" spinbtn[1,100,1] 1 25;\n"
	"property \"Buffer duration (seconds)\" spinbtn[0.1,5.0,0.1] 2 2.0;\n"
;

extern "C" DB_plugin_t * ddb_vlevel_load(DB_functions_t *ddb){
	deadbeef = ddb;
	plugin.plugin.api_vmajor = DB_API_VERSION_MAJOR;
	plugin.plugin.api_vminor = DB_API_VERSION_MINOR;
	plugin.open = vlevel_open;
	plugin.close = vlevel_close;
	plugin.process = vlevel_process;
	plugin.plugin.version_major = 0;
	plugin.plugin.version_minor = 1;
	plugin.plugin.type = DB_PLUGIN_DSP;
	plugin.plugin.id = "vlevel";
	plugin.plugin.name = "VLevel";
	plugin.plugin.descr = "Volume Leveler (VLevel) DSP Plugin";
	plugin.plugin.copyright = "copyright message - author(s), license, etc";
	plugin.plugin.website = "http://example.org";
	plugin.num_params = vlevel_num_params;
	plugin.get_param_name = vlevel_get_param_name;
	plugin.set_param = vlevel_set_param;
	plugin.get_param = vlevel_get_param;
	plugin.reset = vlevel_reset;
	plugin.configdialog = settings_dlg;
	return &plugin.plugin;
}
