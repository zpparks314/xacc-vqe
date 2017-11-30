#ifndef VQETASKS_COMPUTEEXPECTATIONVALUES_HPP_
#define VQETASKS_COMPUTEEXPECTATIONVALUES_HPP_

#include "StatePreparationEvaluator.hpp"
#include "VQETask.hpp"

namespace xacc {
namespace vqe {

class ComputeExpectationValues: public VQETask {

public:

	ComputeExpectationValues() {}

	ComputeExpectationValues(std::shared_ptr<VQEProgram> prog) :
			VQETask(prog) {
	}

	virtual VQETaskResult execute(Eigen::VectorXd parameters);

	/**
	 * Return the name of this instance.
	 *
	 * @return name The string name
	 */
	virtual const std::string name() const {
		return "compute-expectation-values";
	}

	/**
	 * Return the description of this instance
	 * @return description The description of this object.
	 */
	virtual const std::string description() const {
		return "";
	}

	int vqeIteration = 0;
	int totalQpuCalls = 0;

};
}
}
#endif