#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <set>

#include "Visualizer.h"

#include "../Solver/PbReader.h"
#include "../Solver/OilDelivery.pb.h"


using namespace std;
using namespace szx;
using namespace pb;

#define PERIODNUM 4



int vehicleVolume(const pb::OilDelivery_Vehicle& vehicle) {
	int volume = 0;
	for (auto cabin : vehicle.cabins()) { volume += cabin.volume(); }
	return volume;
}

int main(int argc, char *argv[]) {
	enum CheckerFlag {
		IoError = 0x0,
		FormatError = 0x1,
		VehicleDispatchError = 0x2,
		StationOverTimeError = 0x4,
		CabinOverVolumeError = 0x8,
		StationOverDemandError = 0x16
	};

	string inputPath = "..\\Deploy\\Instance\\rand.p4s24v3.json";
	string outputPath = "..\\Deploy\\Solution\\rand.p4s24v3.json";

	//if (argc > 1) {
	//	inputPath = argv[1];
	//}
	//else {
	//	cout << "input path: " << flush;
	//	cin >> inputPath;
	//}

	//if (argc > 2) {
	//	outputPath = argv[2];
	//}
	//else {
	//	cout << "output path: " << flush;
	//	cin >> outputPath;
	//}

	pb::OilDelivery::Input input;
	if (!load(inputPath, input)) { return ~CheckerFlag::IoError; }

	pb::OilDelivery::Output output;
	ifstream ifs(outputPath);
	if (!ifs.is_open()) { return ~CheckerFlag::IoError; }
	string submission;
	std::getline(ifs, submission); // skip the first line.
	ostringstream oss;
	oss << ifs.rdbuf();
	pb::jsonToProtobuf(oss.str(), output);

	// check solution.
	int error = 0;
	double sumTotal = 0.0;	// objective

	int stationNumber = input.gasstations_size();
	int *oilSum = new int[stationNumber]{ 0 };			// oilSum[i]: sum of oil deliveried to gas station i
	int *deliveriedTimes = new int[stationNumber * PERIODNUM]{ 0 };	// deliveried times of each station in all periods

	if (output.deliveries_size() != PERIODNUM) { error |= CheckerFlag::FormatError; }
	int period = 0;
	for (auto delivery = output.deliveries().begin(); delivery != output.deliveries().end(); ++delivery, ++period) {

		set<int> vehicleDeliverySet;
		if (delivery->vehicledeliveries_size() != input.vehicles_size()) { error |= CheckerFlag::VehicleDispatchError; }

		for (auto vehicleDelivery = delivery->vehicledeliveries().begin(); vehicleDelivery != delivery->vehicledeliveries().end(); ++vehicleDelivery) {

			auto vehicleInput = input.vehicles(vehicleDelivery->id());
			// intermediate variables to count objective `sumTotal`
			double vehicleValue = 0.0, fullLoadRate = 0.0, loadSharing = 0.0;
			int vehicleLoad = 0, maxStationId = 0, minStationId = 0;

			// each vehicle must delivery only one time in each period
			if (vehicleDeliverySet.find(vehicleDelivery->id()) != vehicleDeliverySet.end()) { error |= CheckerFlag::VehicleDispatchError; }
			else { vehicleDeliverySet.insert(vehicleDelivery->id()); }

			for (auto cabinDelivery = vehicleDelivery->cabindeliveries().begin(); cabinDelivery != vehicleDelivery->cabindeliveries().end(); ++cabinDelivery) {
				// load over cabin's volume
				if (cabinDelivery->quantity() > vehicleInput.cabins(cabinDelivery->id()).volume() ) { error |= CheckerFlag::CabinOverVolumeError; }
				// load over station's demand
				oilSum[cabinDelivery->stationid()] += cabinDelivery->quantity();
				auto demandValue = input.gasstations(cabinDelivery->stationid()).demandvalues(period);
				if (oilSum[cabinDelivery->stationid()] > demandValue.demand()) { error |= CheckerFlag::StationOverDemandError; }
				// if a station is deliveried in more than one period 
				deliveriedTimes[period*stationNumber + cabinDelivery->stationid()] = 1;
				
				// total value loaded by a vehicle
				vehicleValue += 1.0*(cabinDelivery->quantity())*(demandValue.value()) / (demandValue.demand());
				// loadage of a vehicle
				vehicleLoad += cabinDelivery->quantity();
				// max and min station id
				if (cabinDelivery->stationid() > maxStationId) { maxStationId = cabinDelivery->stationid(); }
				if (cabinDelivery->stationid() < minStationId) { minStationId = cabinDelivery->stationid(); }
			}
			// full load rate
			fullLoadRate = 1.0*vehicleLoad / vehicleVolume(vehicleInput);
			// load sharing
			loadSharing = 1.0*(vehicleInput.cabins_size()) / (vehicleInput.cabins_size() + maxStationId - minStationId);
			// sum value of all vehicles in all periods
			sumTotal += vehicleValue * fullLoadRate * loadSharing;
		}
	}
	// if a station is deliveried in more than one period
	for (int i = 0; i < stationNumber; ++i) {
		int number = 0;
		for (int j = 0; j < PERIODNUM; ++j) {
			number += deliveriedTimes[i + j * stationNumber];
			if (number > 1) {
				error |= CheckerFlag::StationOverTimeError;
				goto end;
			}
		}
	}
	end:
	return (error == 0) ? int(10000*sumTotal) : ~error;
}