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


namespace szx {

#pragma region Solver::Cli
int Solver::Cli::run(int argc, char * argv[]) {
    Log(LogSwitch::Szx::Cli) << "parse command line arguments." << endl;
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

    for (int i = 1; i < argc; ++i) { // skip executable name.
        auto mapIter = optionMap.find(argv[i]);
        if (mapIter != optionMap.end()) { // option argument.
            mapIter->second = argv[++i];
        } else { // switch argument.
            switchSet.insert(argv[i]);
        }
    }

    Log(LogSwitch::Szx::Cli) << "execute commands." << endl;
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

    Log(LogSwitch::Szx::Input) << "load instance " << env.instPath << " (seed=" << env.randSeed << ")." << endl;
    Problem::Input input;
    if (!input.load(env.instPath)) { return -1; }

    Solver solver(input, env, cfg);
    solver.solve();

    pb::OilDelivery_Submission submission;
    submission.set_thread(to_string(env.jobNum));
    submission.set_instance(env.friendlyInstName());
    submission.set_duration(to_string(solver.timer.elapsedSeconds()) + "s");

    solver.output.save(env.slnPath, submission);
    #if SZX_DEBUG
    solver.output.save(env.solutionPathWithTime(), submission);
    solver.record();
    #endif // SZX_DEBUG

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
    // EXTEND[szx][8]: load environment from file.
    // EXTEND[szx][8]: check file existence first.
}

void Solver::Environment::save(const String &filePath) const {
    // EXTEND[szx][8]: save environment to file.
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
    // EXTEND[szx][5]: load configuration from file.
    // EXTEND[szx][8]: check file existence first.
}

void Solver::Configuration::save(const String &filePath) const {
    // EXTEND[szx][5]: save configuration to file.
}
#pragma endregion Solver::Configuration

#pragma region Solver
bool Solver::solve() {
    init();

    int workerNum = (max)(1, env.jobNum / cfg.threadNumPerWorker);
    cfg.threadNumPerWorker = env.jobNum / workerNum;
    List<Solution> solutions(workerNum, Solution(this));
    List<bool> success(workerNum);

    Log(LogSwitch::Szx::Framework) << "launch " << workerNum << " workers." << endl;
    List<thread> threadList;
    threadList.reserve(workerNum);
    for (int i = 0; i < workerNum; ++i) {
        // TODO[szx][2]: as *this is captured by ref, the solver should support concurrency itself, i.e., data members should be read-only or independent for each worker.
        // OPTIMIZE[szx][3]: add a list to specify a series of algorithm to be used by each threads in sequence.
        threadList.emplace_back([&, i]() { success[i] = optimize(solutions[i], i); });
    }
    for (int i = 0; i < workerNum; ++i) { threadList.at(i).join(); }

    Log(LogSwitch::Szx::Framework) << "collect best result among all workers." << endl;
    int bestIndex = -1;
    Length bestValue = 0;
    for (int i = 0; i < workerNum; ++i) {
        if (!success[i]) { continue; }
        Log(LogSwitch::Szx::Framework) << "worker " << i << " got " << solutions[i].sumTotal << endl;
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
    #if SZX_DEBUG
    int generation = 0;

    ostringstream log;

    System::MemoryUsage mu = System::peakMemoryUsage();

    double obj = output.sumTotal;
    Length checkerObj = -1;
    bool feasible = check(checkerObj);

    // record basic information.
	log << env.friendlyLocalTime() << ","
		<< env.rid << ","
		<< env.instPath << ","
		<< feasible << "," << (obj - checkerObj) << ","
		<< output.sumTotal << ","
		<< timer.elapsedSeconds() << ","
		<< mu.physicalMemory << "," << mu.virtualMemory << ","
		<< env.randSeed << ","
		<< cfg.toBriefStr() << ","
		<< generation << "," << iteration << ","
		<< output.sumTotal;

    // record solution vector.
    // EXTEND[szx][2]: save solution in log.
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
    #endif // SZX_DEBUG
}

bool Solver::check(Length &checkerObj) const {
    #if SZX_DEBUG
	enum CheckerFlag {
		IoError = 0x0,
		FormatError = 0x1,
		VehicleDispatchError = 0x2,
		StationOverTimeError = 0x4,
		CabinOverVolumeError = 0x8,
		StationOverDemandError = 0x16
	};

    checkerObj = System::exec("Checker.exe " + env.instPath + " " + env.solutionPathWithTime());
    if (checkerObj > 0) { return true; }
    checkerObj = ~checkerObj;
    if (checkerObj == CheckerFlag::IoError) { Log(LogSwitch::Checker) << "IoError." << endl; }
    if (checkerObj & CheckerFlag::FormatError) { Log(LogSwitch::Checker) << "FormatError." << endl; }
    if (checkerObj & CheckerFlag::VehicleDispatchError) { Log(LogSwitch::Checker) << "VehicleDispatchError." << endl; }
    if (checkerObj & CheckerFlag::StationOverTimeError) { Log(LogSwitch::Checker) << "StationOverTimeError." << endl; }
    if (checkerObj & CheckerFlag::CabinOverVolumeError) { Log(LogSwitch::Checker) << "CabinOverVolumeError." << endl; }
	if (checkerObj & CheckerFlag::StationOverDemandError) { Log(LogSwitch::Checker) << "StationOverDemandError." << endl; }
    return false;
    #else
    checkerObj = 0;
    return true;
    #endif // SZX_DEBUG
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
	Log(LogSwitch::Szx::Framework) << "worker " << workerId << " starts." << endl;

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
			auto cabinsInput = input.vehicles()[v].cabins();
			ID cabinNumber = input.vehicles()[v].cabins_size();
			// intermediate variables to count objective `sumTotal`
			double vehicleValue = 0.0, fullLoadRate = 0.0, loadSharing = 0.0;
			int vehicleLoad = 0, maxStationId = 0, minStationId = 0;

			vehicle->set_id(v);
			for (ID c = 0; c < cabinNumber;++c ) {
				auto cabin = vehicle->add_cabindeliveries();
				cabin->set_id(c);
				int stationId = rand.pick(stationNumber);
				int demand = input.gasstations()[stationId].demandvalues()[i].demand();
				int value = input.gasstations()[stationId].demandvalues()[i].value();
				int maxQuantity = cabinsInput[c].volume() > demand ? demand : cabinsInput[c].volume();
				int quantity = rand.pick(maxQuantity);
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
			fullLoadRate = 1.0*vehicleLoad / vehicleVolume(input.vehicles[v]);
			// load sharing
			loadSharing = (input.vehicles[v].cabins_size()) / (input.vehicles[v].cabins_size() + maxStationId - minStationId);
			// sum value of all vehicles in all periods
			sln.sumTotal += vehicleValue * fullLoadRate * loadSharing;
		}
	}

	Log(LogSwitch::Szx::Framework) << "worker " << workerId << " ends." << endl;
	return status;
}
#pragma endregion Solver

}
