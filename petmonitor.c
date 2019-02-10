#include <stdio.h>
#include <stdlib.h>
#include <phidget22.h>
#include <string.h>
#include <curl/curl.h>
#include <unistd.h>
#include <time.h>

#define BRIDGE_SERIALNUM		141009 //change this to match your serial num
#define DICTIONARY_SERIALNUM	54321 //you may need to change this to match your serial num
#define WATER_BRIDGE_CHANNEL	0 //you may need to change this to match your channel
#define FOOD_BRIDGE_CHANNEL		2 //you may need to change this to match your channel
#define WEIGHT_BRIDGE_CHANNEL 	1 //you may need to change this to match your channel
#define WEIGHT_DATAINT			500
#define WEIGH_TIME				30000

#define FROM    "YOUREMAIL@gmail.com" // change this
#define CURL_USERNAME "YOUREMAIL@gmail.com" // change this
#define CURL_PASSWORD "YOUR EMAIL PASSWORD" // change this
#define CURL_URL "smtp://smtp.gmail.com:587" //CHANGE THIS IF YOU ARE NOT USING GMAIL

#define SLEEP_TIME 5
#define DELAY 600/SLEEP_TIME // 10 mins
#define SUPPLIES_LOW	0.05
#define HOURS_8	28800
#define HOURS_1 3600
struct Supplies {
	double food;
	double water;
};
struct PhoneNumberInfo{
	int count;
	char phonenumbers[5][50];
};
int initialMessage = 1;
int dictionaryUpdate = 1;

static const char *payload_text[] = {
	"Subject: Phidgets Pet Monitor\r\n",
	"\r\n", /* empty line to divide headers from body, see RFC5322 */
	"Your animal's food and or water supply is low!\r\n",
	"\r\n",
	NULL
};

struct upload_status {
	int lines_read;
};

static size_t payload_source(void *ptr, size_t size, size_t nmemb, void *userp)
{
	struct upload_status *upload_ctx = (struct upload_status *)userp;
	const char *data;

	if ((size == 0) || (nmemb == 0) || ((size*nmemb) < 1)) {
		return 0;
	}

	data = payload_text[upload_ctx->lines_read];

	if (data) {
		size_t len = strlen(data);
		memcpy(ptr, data, len);
		upload_ctx->lines_read++;

		return len;
	}

	return 0;
}
/*
int sendMessage(struct PhoneNumberInfo info){

}
*/

double animalsWeight = 0;
int numSamples = 0;
int weighing = 0;

void 
logData(void) {
	time_t rawtime;
	struct tm * timeinfo;
	time(&rawtime);
	timeinfo = localtime(&rawtime);
	FILE* log = fopen("data/weightdata.txt", "a");
	if(log == NULL){
		PhidgetLog_log(PHIDGET_LOG_ERROR,"failed to open weightdata.txt\n");
		return;
	}
	printf("%f%s", animalsWeight,asctime(timeinfo));
	fprintf(log,"%f,%s", animalsWeight, asctime(timeinfo));
	fclose(log);
}

//water, food, weight calibration values
double m[3] = {6.7326,22.4266,0}; //change these calibration values
double b[3] = {-0.6830,-1.3019,0};//change these calibration values
void
onVoltageRatioChangeHandler(PhidgetVoltageRatioInputHandle ch, void *ctx, double ratio) {	
	double weight = m[2]*(ratio*1000.0) + b[2]; //convert to weight
	if(weight > 5){ //if weight > 5 lbs, assume cat is on scale
		if(weighing == 0){
			PhidgetLog_log(PHIDGET_LOG_INFO,"Weighing started");
			weighing = 1;
		}
		numSamples++;
		animalsWeight += weight;
	}else{
		if(weighing){
			PhidgetLog_log(PHIDGET_LOG_INFO,"Weighing ended, logging averaged weight");
			if(numSamples*WEIGHT_DATAINT >= WEIGH_TIME){ //only log if we have at least 30 seconds worth of data 
				animalsWeight /= numSamples;
				logData();
			}
			//reset all variables
			numSamples = 0;
			animalsWeight = 0;
			weighing = 0;
		}
	}
}

void
onDictionaryUpdate(PhidgetDictionaryHandle ch, void *ctx, const char *key, const char *value) {
	dictionaryUpdate = 1;
}

double getVoltageRatio(PhidgetVoltageRatioInputHandle ch,int channel) {
	double value;
	PhidgetVoltageRatioInput_getVoltageRatio(ch, &value);
	return m[channel]*(value*1000.0) + b[channel];
}
int main(void)
{
	PhidgetDictionaryHandle dict;
	PhidgetVoltageRatioInputHandle water;
	PhidgetVoltageRatioInputHandle food;
	PhidgetVoltageRatioInputHandle weight;
	PhidgetReturnCode result;
	struct Supplies supplies;
	struct PhoneNumberInfo phonenumberinfo;
	time_t lastTime;
	char value[50];
	int wait = 0;
	int i;

	PhidgetLog_enable(PHIDGET_LOG_INFO, "petmonitor.log");
	PhidgetNet_enableServerDiscovery(PHIDGETSERVER_DEVICE);

	//create water VoltageRatioInput
	result = PhidgetVoltageRatioInput_create(&water);
	if (result != EPHIDGET_OK) {
		PhidgetLog_log(PHIDGET_LOG_ERROR, "failed to create voltage ratio object");
		return 1;
	}
	result = Phidget_setDeviceSerialNumber((PhidgetHandle)water, BRIDGE_SERIALNUM);
	if (result != EPHIDGET_OK) {
		PhidgetLog_log(PHIDGET_LOG_ERROR, "failed to set device serial number");
		return 1;
	}
	result = Phidget_setChannel((PhidgetHandle)water, WATER_BRIDGE_CHANNEL);
	if (result != EPHIDGET_OK) {
		PhidgetLog_log(PHIDGET_LOG_ERROR, "failed to set channel");
		return 1;
	}
	result = Phidget_openWaitForAttachment((PhidgetHandle)water, 2000);
	if (result != EPHIDGET_OK) {
		PhidgetLog_log(PHIDGET_LOG_ERROR, "failed to open water channel");
		return 1;
	}
	result = PhidgetVoltageRatioInput_setBridgeEnabled(water, 1);
	if (result != EPHIDGET_OK) {
		PhidgetLog_log(PHIDGET_LOG_ERROR, "failed to enable bridge");
		return 1;
	}
	result = PhidgetVoltageRatioInput_setBridgeGain(water, BRIDGE_GAIN_1);
	if (result != EPHIDGET_OK) {
		PhidgetLog_log(PHIDGET_LOG_ERROR, "failed to enable bridge");
		return 1;
	}

	//create food VoltageRatioInput
	result = PhidgetVoltageRatioInput_create(&food);
	if (result != EPHIDGET_OK) {
		PhidgetLog_log(PHIDGET_LOG_ERROR, "failed to create voltage ratio object");
		return 1;
	}
	result = Phidget_setDeviceSerialNumber((PhidgetHandle)food, BRIDGE_SERIALNUM);
	if (result != EPHIDGET_OK) {
		PhidgetLog_log(PHIDGET_LOG_ERROR, "failed to set device serial number");
		return 1;
	}
	result = Phidget_setChannel((PhidgetHandle)food, FOOD_BRIDGE_CHANNEL);
	if (result != EPHIDGET_OK) {
		PhidgetLog_log(PHIDGET_LOG_ERROR, "failed to set channel");
		return 1;
	}
	result = Phidget_openWaitForAttachment((PhidgetHandle)food, 2000);
	if (result != EPHIDGET_OK) {
		PhidgetLog_log(PHIDGET_LOG_ERROR, "failed to open food channel");
		return 1;
	}
	result = PhidgetVoltageRatioInput_setBridgeEnabled(food, 1);
	if (result != EPHIDGET_OK) {
		PhidgetLog_log(PHIDGET_LOG_ERROR, "failed to enable bridge");
		return 1;
	}
	result = PhidgetVoltageRatioInput_setBridgeGain(food, BRIDGE_GAIN_1);
	if (result != EPHIDGET_OK) {
		PhidgetLog_log(PHIDGET_LOG_ERROR, "failed to enable bridge");
		return 1;
	}

	//create weight VoltageRatioInput
	result = PhidgetVoltageRatioInput_create(&weight);
	if (result != EPHIDGET_OK) {
		PhidgetLog_log(PHIDGET_LOG_ERROR, "failed to create voltage ratio object");
		return 1;
	}
	result = Phidget_setDeviceSerialNumber((PhidgetHandle)weight, BRIDGE_SERIALNUM);
	if (result != EPHIDGET_OK) {
		PhidgetLog_log(PHIDGET_LOG_ERROR, "failed to set device serial number");
		return 1;
	}
	result = Phidget_setChannel((PhidgetHandle)weight, WEIGHT_BRIDGE_CHANNEL);
	if (result != EPHIDGET_OK) {
		PhidgetLog_log(PHIDGET_LOG_ERROR, "failed to set channel");
		return 1;
	}
	result = PhidgetVoltageRatioInput_setOnVoltageRatioChangeHandler(weight, onVoltageRatioChangeHandler, NULL);
	if (result != EPHIDGET_OK) {
		PhidgetLog_log(PHIDGET_LOG_ERROR, "failed to set on voltage ratio change handler");
		return 1;
	}
	result = Phidget_openWaitForAttachment((PhidgetHandle)weight, 2000);
	if (result != EPHIDGET_OK) {
		PhidgetLog_log(PHIDGET_LOG_ERROR, "failed to open food channel");
		return 1;
	}
	result = PhidgetVoltageRatioInput_setBridgeEnabled(weight, 1);
	if (result != EPHIDGET_OK) {
		PhidgetLog_log(PHIDGET_LOG_ERROR, "failed to enable bridge");
		return 1;
	}
	result = PhidgetVoltageRatioInput_setBridgeGain(weight, BRIDGE_GAIN_1);
	if (result != EPHIDGET_OK) {
		PhidgetLog_log(PHIDGET_LOG_ERROR, "failed to enable bridge");
		return 1;
	}
	result = PhidgetVoltageRatioInput_setDataInterval(weight, WEIGHT_DATAINT);
	if (result != EPHIDGET_OK) {
		PhidgetLog_log(PHIDGET_LOG_ERROR, "failed to set data interval");
		return 1;
	}
	//create dictionary
	result = PhidgetDictionary_create(&dict);
	if (result != EPHIDGET_OK) {
		PhidgetLog_log(PHIDGET_LOG_ERROR, "failed to create dictionary object");
		return 1;
	}
	result = Phidget_setDeviceSerialNumber((PhidgetHandle)dict, DICTIONARY_SERIALNUM);
	if (result != EPHIDGET_OK) {
		PhidgetLog_log(PHIDGET_LOG_ERROR, "failed to set device serial number");
		return 1;
	}
	result = PhidgetDictionary_setOnUpdateHandler(dict, onDictionaryUpdate, NULL);
	if (result != EPHIDGET_OK) {
		PhidgetLog_log(PHIDGET_LOG_ERROR, "failed to set dictionary update handler\n");
		return 1;
	}
	result = Phidget_openWaitForAttachment((PhidgetHandle)dict, 2000);
	if (result != EPHIDGET_OK) {
		PhidgetLog_log(PHIDGET_LOG_ERROR, "failed to open dictionary channel");
		return 1;
	}

	sleep(2);
	
	while (1) {
		supplies.water = getVoltageRatio(water,0);
		supplies.food = getVoltageRatio(food,1);
		snprintf(value, 20, "%f", supplies.water*100.0);
		PhidgetDictionary_set(dict, "waterSupply", value);
		snprintf(value, 20, "%f", supplies.food*100.0);
		PhidgetDictionary_set(dict, "foodSupply", value);

		if (dictionaryUpdate) {
			dictionaryUpdate = 0;
			PhidgetLog_log(PHIDGET_LOG_INFO, "dictionary update");
			PhidgetDictionary_get(dict, "phonenumbercount", value, 50);
			phonenumberinfo.count = strtol(value, NULL, 0);
			for (i = 0; i < count; i++) {
				snprintf(value, 50, "phonenumber%d", i);
				PhidgetDictionary_get(dict, value, phonenumberinfo.phonenumbers[i], 50);
			}
		}

		if (supplies.food < SUPPLIES_LOW || supplies.water < SUPPLIES_LOW) {
			if (wait++ == DELAY) {
				wait = 0;
				time_t currentTime;
				time(&currentTime);
				if(initialMessage || difftime(currentTime,lastTime) > HOURS_8){
					PhidgetLog_log(PHIDGET_LOG_INFO, "supplies are low, sending text message");
					initialMessage = 0;
					time(&lastTime);
					PhidgetLog_log(PHIDGET_LOG_INFO, "trying to send text message");
					//sendMessage(phonenumberinfo);
				}
			}
		}
		else {
			wait = 0;
		}

		sleep(SLEEP_TIME); //sleep in seconds
	}
	return 0;
}