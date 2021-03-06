#include "Solver.h"

#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <mutex>
#include <cmath>

using namespace std;

namespace lcg {

#pragma region Solver::Cli
int Solver::Cli::run(int argc, char * argv[]) {
    Log(LogSwitch::LCG::Cli) << "parse command line arguments." << endl;
    Set<String> switchSet;
    Map<String, char*> optionMap({ // use string as key to compare string contents instead of pointers.
        { InstancePathOption(), nullptr },
        { SolutionPathOption(), nullptr },
        { RandSeedOption(), nullptr },
        { TimeoutOption(), nullptr },
        { MaxIterOption(), nullptr },
        { JobNumOption(), nullptr },
        { RunIdOption(), nullptr },
        { EnvironmentPathOption(), nullptr },
        { ConfigPathOption(), nullptr },
        { LogPathOption(), nullptr }
    });

    for (int i = 1; i < argc; ++i) {		// skip executable name.
        auto mapIter = optionMap.find(argv[i]);
        if (mapIter != optionMap.end()) {	// option argument.
            mapIter->second = argv[++i];
        } else {	// switch argument.
            switchSet.insert(argv[i]);
        }
    }

    Log(LogSwitch::LCG::Cli) << "execute commands." << endl;
    if (switchSet.find(HelpSwitch()) != switchSet.end()) {
        cout << HelpInfo() << endl;
    }

    if (switchSet.find(AuthorNameSwitch()) != switchSet.end()) {
        cout << AuthorName() << endl;
    }

    Solver::Environment env;
    env.load(optionMap);
    if (env.instPath.empty() || env.slnPath.empty()) { return -1; }

    Solver::Configuration cfg;
    cfg.load(env.cfgPath);

    Log(LogSwitch::LCG::Input) << "load instance " << env.instPath << " (seed=" << env.randSeed << ")." << endl;
    Problem::Input input;
    if (!input.load(env.instPath)) { return -1; }

    Solver solver(input, env, cfg);
    solver.solve();

    pb::OilDelivery_Submission submission;
    submission.set_thread(to_string(env.jobNum));
    submission.set_instance(env.friendlyInstName());
    submission.set_duration(to_string(solver.timer.elapsedSeconds()) + "s");

    solver.output.save(env.slnPath, submission);
    #if LCG_DEBUG
    solver.output.save(env.solutionPathWithTime(), submission);
    solver.record();
    #endif // LCG_DEBUG

    return 0;
}
#pragma endregion Solver::Cli

#pragma region Solver::Environment
void Solver::Environment::load(const Map<String, char*> &optionMap) {
    char *str;

    str = optionMap.at(Cli::EnvironmentPathOption());
    if (str != nullptr) { loadWithoutCalibrate(str); }

    str = optionMap.at(Cli::InstancePathOption());
    if (str != nullptr) { instPath = str; }

    str = optionMap.at(Cli::SolutionPathOption());
    if (str != nullptr) { slnPath = str; }

    str = optionMap.at(Cli::RandSeedOption());
    if (str != nullptr) { randSeed = atoi(str); }

    str = optionMap.at(Cli::TimeoutOption());
    if (str != nullptr) { msTimeout = static_cast<Duration>(atof(str) * Timer::MillisecondsPerSecond); }

    str = optionMap.at(Cli::MaxIterOption());
    if (str != nullptr) { maxIter = atoi(str); }

    str = optionMap.at(Cli::JobNumOption());
    if (str != nullptr) { jobNum = atoi(str); }

    str = optionMap.at(Cli::RunIdOption());
    if (str != nullptr) { rid = str; }

    str = optionMap.at(Cli::ConfigPathOption());
    if (str != nullptr) { cfgPath = str; }

    str = optionMap.at(Cli::LogPathOption());
    if (str != nullptr) { logPath = str; }

    calibrate();
}

void Solver::Environment::load(const String &filePath) {
    loadWithoutCalibrate(filePath);
    calibrate();
}

void Solver::Environment::loadWithoutCalibrate(const String &filePath) {
    // EXTEND[lcg][8]: load environment from file.
    // EXTEND[lcg][8]: check file existence first.
}

void Solver::Environment::save(const String &filePath) const {
    // EXTEND[lcg][8]: save environment to file.
}
void Solver::Environment::calibrate() {
    // adjust thread number.
    int threadNum = thread::hardware_concurrency();
    if ((jobNum <= 0) || (jobNum > threadNum)) { jobNum = threadNum; }

    // adjust timeout.
    msTimeout -= Environment::SaveSolutionTimeInMillisecond;
}
#pragma endregion Solver::Environment

#pragma region Solver::Configuration
void Solver::Configuration::load(const String &filePath) {
    // EXTEND[lcg][5]: load configuration from file.
    // EXTEND[lcg][8]: check file existence first.
}

void Solver::Configuration::save(const String &filePath) const {
    // EXTEND[lcg][5]: save configuration to file.
}
#pragma endregion Solver::Configuration

#pragma region Solver
bool Solver::solve() {
    init();

    int workerNum = (max)(1, env.jobNum / cfg.threadNumPerWorker);
    cfg.threadNumPerWorker = env.jobNum / workerNum;
    List<Solution> solutions(workerNum, Solution(this));
    List<bool> success(workerNum);

    Log(LogSwitch::LCG::Framework) << "launch " << workerNum << " workers." << endl;
    List<thread> threadList;
    threadList.reserve(workerNum);
    for (int i = 0; i < workerNum; ++i) {
        // TODO[lcg][2]: as *this is captured by ref, the solver should support concurrency itself, i.e., data members should be read-only or independent for each worker.
        // OPTIMIZE[lcg][3]: add a list to specify a series of algorithm to be used by each threads in sequence.
        threadList.emplace_back([&, i]() { success[i] = optimize(solutions[i], i); });
    }
    for (int i = 0; i < workerNum; ++i) { threadList.at(i).join(); }

    Log(LogSwitch::LCG::Framework) << "collect best result among all workers." << endl;
    int bestIndex = -1;
    Revenue bestValue = 0.0;
    for (int i = 0; i < workerNum; ++i) {
        if (!success[i]) { continue; }
        Log(LogSwitch::LCG::Framework) << "worker " << i << " got " << solutions[i].sumTotal << endl;
        if (solutions[i].sumTotal <= bestValue) { continue; }
        bestIndex = i;
        bestValue = solutions[i].sumTotal;
    }

    env.rid = to_string(bestIndex);
    if (bestIndex < 0) { return false; }
    output = solutions[bestIndex];
    return true;
}

void Solver::record() const {
    #if LCG_DEBUG
    int generation = 0;

    ostringstream log;

    System::MemoryUsage mu = System::peakMemoryUsage();

    Revenue obj = output.sumTotal;
	Revenue checkerObj = -1.0;
    bool feasible = check(checkerObj);
	Revenue diff = obj - checkerObj;

    // record basic information.
	log << env.friendlyLocalTime() << ","
		<< env.rid << ","
		<< env.instPath << ","
		<< feasible << "," << (diff < 10E-3 ? 0 : diff) << ","	// the difference between solution and checker.
		<< obj << ","
		<< timer.elapsedSeconds() << ","
		<< mu.physicalMemory << "," << mu.virtualMemory << ","
		<< env.randSeed << ","
		<< cfg.toBriefStr() << ","
		<< generation << "," << iteration << ","
		<< obj;

    // record solution vector.
    // EXTEND[lcg][2]: save solution in log.
    log << endl;

    // append all text atomically.
    static mutex logFileMutex;
    lock_guard<mutex> logFileGuard(logFileMutex);

    ofstream logFile(env.logPath, ios::app);
    logFile.seekp(0, ios::end);
    if (logFile.tellp() <= 0) {
        logFile << "Time,ID,Instance,Feasible,ObjMatch,Width,Duration,PhysMem,VirtMem,RandSeed,Config,Generation,Iteration,Ratio,Solution" << endl;
    }
    logFile << log.str();
    logFile.close();
    #endif // LCG_DEBUG
}

bool Solver::check(Revenue &checkerObj) const {
    #if LCG_DEBUG
	enum CheckerFlag {
		IoError = 0x0,
		FormatError = 0x1,
		VehicleDispatchError = 0x2,
		StationOverTimeError = 0x4,
		CabinOverVolumeError = 0x8,
		StationOverDemandError = 0x16
	};

	int checkerDecimal = System::exec("Checker.exe " + env.instPath + " " + env.solutionPathWithTime());
	checkerObj = 1.0*checkerDecimal / 10000;
    if (checkerDecimal > 0) { return true; }
	checkerDecimal = ~checkerDecimal;
    if (checkerDecimal == CheckerFlag::IoError) { Log(LogSwitch::Checker) << "IoError." << endl; }
    if (checkerDecimal & CheckerFlag::FormatError) { Log(LogSwitch::Checker) << "FormatError." << endl; }
    if (checkerDecimal & CheckerFlag::VehicleDispatchError) { Log(LogSwitch::Checker) << "VehicleDispatchError." << endl; }
    if (checkerDecimal & CheckerFlag::StationOverTimeError) { Log(LogSwitch::Checker) << "StationOverTimeError." << endl; }
    if (checkerDecimal & CheckerFlag::CabinOverVolumeError) { Log(LogSwitch::Checker) << "CabinOverVolumeError." << endl; }
	if (checkerDecimal & CheckerFlag::StationOverDemandError) { Log(LogSwitch::Checker) << "StationOverDemandError." << endl; }
    return false;
    #else
    checkerObj = 0.0;
    return true;
    #endif // LCG_DEBUG
}

void Solver::init() {
	periodNumber = 4;
}

int Solver::vehicleVolume(const pb::OilDelivery_Vehicle& vehicle) {
	int volume = 0;
	for (auto cabin : vehicle.cabins()) { volume += cabin.volume(); }
	return volume;
}

bool Solver::optimize(Solution &sln, ID workerId) {
	Log(LogSwitch::LCG::Framework) << "worker " << workerId << " starts." << endl;

	ID stationNumber = input.gasstations_size();
	ID vehicleNumber = input.vehicles_size();

	bool status = true;
	auto &deliveries(*sln.mutable_deliveries());
	deliveries.Reserve(periodNumber);
	sln.sumTotal = 0.0;

	// TODO[0]: replace the following random assignment with your own algorithm.
	for (int i = 0; !timer.isTimeOut() && (i < periodNumber); ++i) {
		auto &delivery(*sln.add_deliveries());

		for (ID v = 0; v < vehicleNumber;++v) {
			auto vehicle = delivery.add_vehicledeliveries();
			ID cabinNumber = input.vehicles(v).cabins_size();
			// intermediate variables to count objective `sumTotal`
			double vehicleValue = 0.0, fullLoadRate = 0.0, loadSharing = 0.0;
			int vehicleLoad = 0, maxStationId = 0, minStationId = 0;

			vehicle->set_id(v);
			for (ID c = 0; c < cabinNumber;++c ) {
				auto cabinInput = input.vehicles(v).cabins(c);
				auto cabin = vehicle->add_cabindeliveries();
				cabin->set_id(c);
				int stationId = rand.pick(0, stationNumber);
				int demand = input.gasstations(stationId).demandvalues(i).demand();
				int value = input.gasstations(stationId).demandvalues(i).value();
				int maxQuantity = cabinInput.volume() > demand ? demand : cabinInput.volume();
				int quantity = rand.pick(0, maxQuantity + 1);
				cabin->set_stationid(stationId);
				cabin->set_quantity(quantity);


				// total value loaded by a vehicle
				vehicleValue += 1.0*quantity * value / demand;
				// loadage of a vehicle
				vehicleLoad += quantity;
				// max and min station id
				if (stationId > maxStationId) { maxStationId = stationId; }
				if (stationId < minStationId) { minStationId = stationId; }
			}
			// full load rate
			fullLoadRate = 1.0*vehicleLoad / vehicleVolume(input.vehicles(v));
			// load sharing
			
			loadSharing = 1.0*cabinNumber / (cabinNumber + maxStationId - minStationId);
			// sum value of all vehicles in all periods
			sln.sumTotal += vehicleValue * fullLoadRate * loadSharing;
		}
	}

	Log(LogSwitch::LCG::Framework) << "worker " << workerId << " ends." << endl;
	return status;
}
#pragma endregion Solver

}
