#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <deadbeef/deadbeef.h>
#include "volumeleveler.h"

enum{
	VLEVEL_PARAM_STRENGTH,
	VLEVEL_PARAM_MAX_MULTIPLIER,
	VLEVEL_PARAM_MAX_BUFFER_LENGTH,
	VLEVEL_PARAM_COUNT
};

DB_functions_t *deadbeef;
DB_dsp_t plugin;

// Only used for the communication between Exchange in volumeleveler.h and samples in vlevel_process.
class ddb_vlevel_buffer{
	public:
	value_t **buffers;
	int count;
	int channels;
	int sample_rate;

	ddb_vlevel_buffer(){
		this->buffers = NULL;
		this->count = 0;
		this->channels = 0;
		this->sample_rate = 0;
	}

	bool init(int count,int channels,int sample_rate){
		if(count <= 0 || channels <= 0) goto fail;
		if(this->buffers){
			value_t *tmp;
			if(
				!(tmp = (value_t *) realloc(*this->buffers,sizeof(value_t) * count * channels))
				|| !(this->buffers = (value_t **) realloc(this->buffers,sizeof(value_t *) * channels))
			){
				goto fail;
			}
			*this->buffers = tmp;
		}else if(!(this->buffers = (value_t **) malloc(sizeof(value_t *) * channels)) || !(*this->buffers = (value_t *) malloc(sizeof(value_t) * count * channels))){
			goto fail;
		}

		for(int c=1; c<channels; c+=1){
			this->buffers[c] = this->buffers[0] + (count * c);
		}
		this->count = count;
		this->channels = channels;
		this->sample_rate = sample_rate;
		return true;

		fail:
		this->del();
		return false;
	}

	void del(){
		if(this->buffers){
			if(this->buffers[0]) free((void*)this->buffers[0]);
			free((void*)this->buffers);
			this->buffers = NULL;
		}
		this->count = 0;
		this->channels = 0;
		this->sample_rate = 0;
	}

	void from_interleaved_channels(const value_t *data){
		for(size_t s=0; s<(size_t)this->count; s+=1){
			for(size_t c=0; c<(size_t)this->channels; c+=1) {
				this->buffers[c][s] = data[s * this->channels + c];
			}
		}
	}

	void to_interleaved_channels(value_t *data,size_t offset){
		for(size_t s=offset; s<(size_t)this->count; s+=1){
		    for(size_t c=0; c<(size_t)this->channels; c+=1){
		        data[(s - offset) * this->channels + c] = this->buffers[c][s];
		    }
		}
	}
};

struct ddb_vlevel_t{
	ddb_dsp_context_t ctx;
	VolumeLeveler *vl;
	ddb_vlevel_buffer buffer;

	//Settings.
	value_t strength;       //Range: 0.0 to 1.0.
	value_t max_multiplier; //Range: Positive.
	double buffer_length;   //Range: Positive.
};

ddb_dsp_context_t *vlevel_open(void){
	ddb_vlevel_t *data = (ddb_vlevel_t *) malloc(sizeof(ddb_vlevel_t));
	DDB_INIT_DSP_CONTEXT(data,ddb_vlevel_t,&plugin);

	//Initialise.
	data->strength = 0.8;
	data->max_multiplier = 25;
	data->buffer_length = 2.0;
	data->buffer = ddb_vlevel_buffer();
	data->vl = new VolumeLeveler();
	data->vl->SetStrength(data->strength);
	data->vl->SetMaxMultiplier(data->max_multiplier);

	return(ddb_dsp_context_t *)data;
}

void vlevel_close(ddb_dsp_context_t *ctx){
	ddb_vlevel_t *data = (ddb_vlevel_t *)ctx;
	data->buffer.del();
	free(data);
}

void vlevel_reset(ddb_dsp_context_t *ctx){
	ddb_vlevel_t *data = (ddb_vlevel_t *)ctx;
	//data->buffer.del();
	data->vl->Flush();
}

//TODO: There is a delay due to the buffer length. The duration indicator after seeking becomes incorrect. Songs also stop early (last part of buffer gets deleted before it has ended). Is there a way to fix this?
int vlevel_process(ddb_dsp_context_t *ctx, float *samples, int nframes, int maxframes, ddb_waveformat_t *fmt, float *r){
	ddb_vlevel_t *data = (ddb_vlevel_t *)ctx;

	if(!data->buffer.buffers || data->buffer.count != nframes || data->buffer.channels != fmt->channels || data->buffer.sample_rate != fmt->samplerate){
		if(data->buffer.init(nframes,fmt->channels,fmt->samplerate)){
			data->vl->SetSamplesAndChannels((size_t)(data->buffer_length * data->buffer.sample_rate + 0.5),data->buffer.channels);
		}else{
			return 0;
		}
	}

	data->buffer.from_interleaved_channels(samples);
	size_t nsilent = data->vl->Exchange<value_t **,bufferExchangePtrPtrIndex>(data->buffer.buffers,data->buffer.buffers,data->buffer.count);
	data->buffer.to_interleaved_channels(samples,nsilent);

	return nframes - nsilent;
}

const char *vlevel_get_param_name(int p){
	switch(p){
	case VLEVEL_PARAM_STRENGTH         : return "Strength";
	case VLEVEL_PARAM_MAX_MULTIPLIER   : return "Max multiplier";
	case VLEVEL_PARAM_MAX_BUFFER_LENGTH: return "Buffer length";
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
		data->strength = atof(val);
		data->vl->SetStrength(data->strength);
		break;
	case VLEVEL_PARAM_MAX_MULTIPLIER:
		data->max_multiplier = atof(val);
		data->vl->SetMaxMultiplier(data->max_multiplier);
		break;
	case VLEVEL_PARAM_MAX_BUFFER_LENGTH:
		data->buffer_length = atof(val);
		data->vl->SetSamplesAndChannels((size_t)(data->buffer_length * data->buffer.sample_rate + 0.5),data->buffer.channels);
		break;
	default:
		fprintf(stderr, "vlevel_param: invalid param index (%d)\n", p);
	}
}

void vlevel_get_param(ddb_dsp_context_t *ctx, int p, char *val, int sz){
	ddb_vlevel_t *data = (ddb_vlevel_t *)ctx;
	switch(p){
	case VLEVEL_PARAM_STRENGTH         : snprintf(val, sz, "%f", data->strength);       break;
	case VLEVEL_PARAM_MAX_MULTIPLIER   : snprintf(val, sz, "%f", data->max_multiplier); break;
	case VLEVEL_PARAM_MAX_BUFFER_LENGTH: snprintf(val, sz, "%f", data->buffer_length);  break;
	default:
		fprintf(stderr, "vlevel_get_param: invalid param index (%d)\n", p);
	}
}

const char settings_dlg[] =
	"property \"Strength\" spinbtn[0.00,1.00,0.01] 0 0.8;\n"
	"property \"Max multiplier (20*log10(m) dB)\" spinbtn[1,100,1] 1 25;\n"
	"property \"Buffer length (seconds)\" spinbtn[0.1,5.0,0.1] 2 2.0;\n"
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
