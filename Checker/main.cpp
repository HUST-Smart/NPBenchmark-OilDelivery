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

#define PeriodNum 4



int vehicleVolume(const pb::OilDelivery_Vehicle& vehicle) {
	int volume = 0;
	for (auto cabin : vehicle.cabins()) {
		volume += cabin.volume();
	}
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

	string inputPath;
	string outputPath;

	if (argc > 1) {
		inputPath = argv[1];
	}
	else {
		cout << "input path: " << flush;
		cin >> inputPath;
	}

	if (argc > 2) {
		outputPath = argv[2];
	}
	else {
		cout << "output path: " << flush;
		cin >> outputPath;
	}

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
	int *deliveriedTimes = new int[stationNumber * PeriodNum]{ 0 };	// deliveried times of each station in all periods

	if (output.deliveries_size() != PeriodNum) { error |= CheckerFlag::FormatError; }
	int period = 0;
	for (auto delivery = output.deliveries().begin(); delivery != output.deliveries().end(); ++delivery, ++period) {

		set<int> vehicleSet;
		if (delivery->vehicles_size() != input.vehicles_size()) { error |= CheckerFlag::VehicleDispatchError; }

		for (auto vehicle = delivery->vehicles().begin(); vehicle != delivery->vehicles().end(); ++vehicle) {

			// intermediate variables to count objective `sumTotal`
			double vehicleValue = 0.0, fullLoadRate = 0.0, loadSharing = 0.0;
			int vehicleLoad = 0, maxStationId = 0, minStationId = 0;

			// each vehicle must delivery only one time in each period
			if (vehicleSet.find(vehicle->id()) != vehicleSet.end()) { error |= CheckerFlag::VehicleDispatchError; }
			else { vehicleSet.insert(vehicle->id()); }

			for (auto cabin = vehicle->cabins().begin(); cabin != vehicle->cabins().end(); ++cabin) {
				// load over cabin's volume
				if (cabin->load() > cabin->volume()) { error |= CheckerFlag::CabinOverVolumeError; }
				// load over station's demand
				oilSum[cabin->stationid()] += cabin->load();
				const auto &demandValue = input.gasstations()[cabin->stationid()].demandvalues();
				if (oilSum[cabin->stationid()] > demandValue[period].demand()) { error |= CheckerFlag::StationOverDemandError; }
				// if a station is deliveried in more than one period 
				deliveriedTimes[period*stationNumber + cabin->stationid()] = 1;
				
				// total value loaded by a vehicle
				vehicleValue += (cabin->load())*(demandValue[period].value()) / (demandValue[period].demand());
				// loadage of a vehicle
				vehicleLoad += cabin->load();
				// max and min station id
				if (cabin->stationid() > maxStationId) { maxStationId = cabin->stationid(); }
				if (cabin->stationid() < minStationId) { minStationId = cabin->stationid(); }
			}
			// full load rate
			fullLoadRate = vehicleLoad / vehicleVolume(*vehicle);
			// load sharing
			loadSharing = (vehicle->cabins_size()) / (vehicle->cabins_size() + maxStationId - minStationId);
			// sum value of all vehicles in all periods
			sumTotal += vehicleValue * fullLoadRate * loadSharing;
		}
	}
	// if a station is deliveried in more than one period
	for (int i = 0; i < stationNumber; ++i) {
		int number = 0;
		for (int j = 0; j < PeriodNum; ++j) {
			number += deliveriedTimes[i + j * stationNumber];
			if (number > 1) {
				error |= CheckerFlag::StationOverTimeError;
				goto end;
			}
		}
	}
	end:
	return (error == 0) ? sumTotal : ~error;
}
//{
//	int f = 0;
//	for (auto gate = output.assignments().begin(); gate != output.assignments().end(); ++gate, ++f) {
//		// check constraints.
//		if ((*gate < 0) || (*gate >= input.airport().gates().size())) { error |= CheckerFlag::FlightNotAssignedError; }
//		for (auto ig = input.flights(f).incompatiblegates().begin(); ig != input.flights(f).incompatiblegates().end(); ++ig) {
//			if (*gate == *ig) { error |= CheckerFlag::IncompatibleAssignmentError; }
//		}
//		const auto &flight(input.flights(f));
//		for (auto flight1 = input.flights().begin(); flight1 != input.flights().end(); ++flight1) {
//			if (*gate != output.assignments(flight1->id())) { continue; }
//			int gap = max(flight.turnaround().begin() - flight1->turnaround().end(),
//				flight1->turnaround().begin() - flight.turnaround().begin());
//			if (gap < input.airport().gates(*gate).mingap()) { error |= CheckerFlag::FlightOverlapError; }
//		}
//
//		// check objective.
//		if (*gate < input.airport().bridgenum()) { ++flightNumOnBridge; }
//	}
//
//	//// visualize solution.
//	//double pixelPerMinute = 1;
//	//double pixelPerGate = 30;
//	//int horizonLen = 0;
//	//for (auto flight = input.flights().begin(); flight != input.flights().end(); ++flight) {
//	//    horizonLen = max(horizonLen, flight->turnaround().end());
//	//}
//
//	//auto pos = outputPath.find_last_of('/');
//	//string outputName = (pos == string::npos) ? outputPath : outputPath.substr(pos + 1);
//	//Drawer draw;
//	//draw.begin("Visualization/" + outputName + ".html", horizonLen * pixelPerMinute, input.airport().gates().size() * pixelPerGate, 1, 0);
//	f = 0;
//	for (auto gate = output.assignments().begin(); gate != output.assignments().end(); ++gate, ++f) {
//		// check constraints.
//		if ((*gate < 0) || (*gate >= input.airport().gates().size())) { continue; }
//		bool incompat = false;
//		for (auto ig = input.flights(f).incompatiblegates().begin(); ig != input.flights(f).incompatiblegates().end(); ++ig) {
//			if (*gate == *ig) { incompat = true; break; }
//		}
//		const auto &flight(input.flights(f));
//		draw.rect(flight.turnaround().begin() * pixelPerMinute, *gate * pixelPerGate,
//			(flight.turnaround().end() - flight.turnaround().begin()) * pixelPerMinute, pixelPerGate,
//			false, to_string(f), "000000", incompat ? "00c00080" : "4080ff80");
//	}
//	draw.end();
//
//	return (error == 0) ? flightNumOnBridge : ~error;
//}
//
