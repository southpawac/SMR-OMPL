///////////////////////////////////////
// COMP/ELEC/MECH 450/550
// Project 4
// Authors: Aidan Curtis & Patrick Han
//////////////////////////////////////

#include "SMR.h"
#include "ompl/base/goals/GoalSampleableRegion.h"
#include "ompl/tools/config/SelfConfig.h"
#include <ompl/control/spaces/RealVectorControlSpace.h>
#include <ompl/control/spaces/DiscreteControlSpace.h>
#include <ompl/base/spaces/RealVectorStateSpace.h>
#include <ompl/base/spaces/SO2StateSpace.h>
#include <ompl/base/spaces/DiscreteStateSpace.h>

#include <limits>
using namespace std;

ompl::control::SMR::SMR(const SpaceInformationPtr &si) : base::Planner(si, "SMR")
{
	specs_.approximateSolutions = true;
	siC_ = si.get();

	Planner::declareParam<double>("goal_bias", this, &SMR::setGoalBias, &SMR::getGoalBias, "0.:.05:1.");
	Planner::declareParam<bool>("intermediate_states", this, &SMR::setIntermediateStates, &SMR::getIntermediateStates);
}

ompl::control::SMR::~SMR() // destructor
{
	freeMemory();
}

void ompl::control::SMR::setup()
{
	base::Planner::setup();
	if (!nn_)
		nn_.reset(tools::SelfConfig::getDefaultNearestNeighbors<Node *>(this));
	nn_->setDistanceFunction([this](const Node *a, const Node *b)
							 {
								 return distanceFunction(a, b);
							 });
}

void ompl::control::SMR::clear()
{
	Planner::clear();
	sampler_.reset();
	freeMemory();
	if (nn_)
		nn_->clear();
	lastGoalNode_ = nullptr;
}

void ompl::control::SMR::freeMemory()
{
	if (nn_)
	{
		std::vector<Node *> nodes;
		nn_->list(nodes);
		for (auto &node : nodes)
		{
			if (node->state)
				si_->freeState(node->state);
			if (node->control)
				siC_->freeControl(node->control);
			delete node;
		}
	}
}


void ompl::control::SMR::GetTransisions(Node *start_state, Control *control, int num_transitions){
	// create an empty list of nodes (R)
	for (int m = 0; m < num_transitions; m += 1){
		ompl::base::State *prop_state = si_->allocState();
		siC_->propagate(start_state->state, control, 3, prop_state);
		auto *new_node = new Node(siC_);
		new_node->state = prop_state;

		// Cast the state to a compound state
		// auto compound_state = start_state->state->as<ompl::base::CompoundState>();
		// const ompl::base::RealVectorStateSpace::StateType* r2;
		// r2 = compound_state->as<ompl::base::RealVectorStateSpace::StateType>(0);
		// const ompl::base::SO2StateSpace::StateType* so2;
		// so2 = compound_state->as<ompl::base::SO2StateSpace::StateType>(1);
		// const ompl::base::DiscreteStateSpace::StateType* d;
		// d = compound_state->as<ompl::base::DiscreteStateSpace::StateType>(2);
		// cout<<"Old Propagate"<<endl;
		// cout<<r2->values[0]<<endl;
		// cout<<r2->values[1]<<endl;
		// cout<<so2->value<<endl;

		// // Cast the state to a compound state
		// compound_state = prop_state->as<ompl::base::CompoundState>();
		// r2 = compound_state->as<ompl::base::RealVectorStateSpace::StateType>(0);
		// so2 = compound_state->as<ompl::base::SO2StateSpace::StateType>(1);
		// d = compound_state->as<ompl::base::DiscreteStateSpace::StateType>(2);
		// cout<<"New Propagate"<<endl;
		// cout<<r2->values[0]<<endl;
		// cout<<r2->values[1]<<endl;
		// cout<<so2->value<<endl;

		Node *neighbor = nn_->nearest(new_node);

		if(control->as<ompl::control::DiscreteControlSpace::ControlType>()->value == 0) {
			start_state->state_control_0.push_back(neighbor);
		} else {
			start_state->state_control_1.push_back(neighbor);
		}
	}

}
void ompl::control::SMR::BuildSMR(int num_samples, int num_transitions){
	for (int i = 0; i < num_samples; i+=1){
		// Uniformly sample a state and add it to the data structure
		auto *new_node = new Node(siC_);
		ompl::base::State *new_state = si_->allocState();
		sampler_->sampleUniform(new_state);
		new_node->state = new_state;
		nn_->add(new_node);
	}

	std::vector<Node *> state_list;
	nn_->list(state_list);
	for (int i = 0; i < int(state_list.size()); i+=1) {
			auto icontrol = state_list[i]->control->as<ompl::control::DiscreteControlSpace::ControlType>(); // cast control to desired type
			icontrol->value = 0;
			GetTransisions(state_list[i], state_list[i]->control, num_transitions);
	}
}



ompl::base::PlannerStatus ompl::control::SMR::solve(const base::PlannerTerminationCondition &ptc)
{

	int NUM_SAMPLES = 50000;
	int NUM_TRANSITIONS = 1;
	double dist = 0.1;
	checkValidity();
	base::Goal *goal = pdef_->getGoal().get();
	auto *goal_s = dynamic_cast<base::GoalSampleableRegion *>(goal);
	while (const base::State *st = pis_.nextStart())
	{
		auto *node = new Node(siC_);
		si_->copyState(node->state, st);
		siC_->nullControl(node->control);
		nn_->add(node); // add our input motions to the nearest neighbor structure
	}

	if (nn_->size() == 0)
	{
		OMPL_ERROR("%s: There are no valid initial states!", getName().c_str());
		return base::PlannerStatus::INVALID_START;
	}

	if (!sampler_)
		sampler_ = si_->allocStateSampler();
	std::cout<<"Buiding SMR"<<std::endl;
	BuildSMR(NUM_SAMPLES, NUM_TRANSITIONS);
	std::cout<<"Value Iteration"<<std::endl;

	// Create the values/rewards and init them to zero
	std::vector<Node *> state_list;
	nn_->list(state_list);
	std::multimap<Node*, double> values; 
	std::multimap<Node*, double> R;

	for (int i = 0; i < int(state_list.size()); i+=1){
		if(goal->isSatisfied(state_list[i]->state, &dist)){
			R.insert({state_list[i], 1.0});
			values.insert({state_list[i], 1.0});
		} else {
			R.insert({state_list[i], 0.0});
			values.insert({state_list[i], 0.0});
		}
	}

	// change is the termination variable
	bool change = true;
	int iteration = 0;
	while(change){
		// cout<<"start"<<endl;
		change = false;
		// Need to recalculate the value for every state. Loop through each state
		for (int i = 0; i < int(state_list.size()); i+=1){
			// For this state that we are iterating over, we need to examine it's transitions
			// Action 0 transitions
			if(!goal->isSatisfied(state_list[i]->state, &dist)){
				double new_value = 0;
				for (int new_state_index = 0; new_state_index < int((state_list[i]->state_control_0).size()); new_state_index++)
				{
					for (auto itr = values.find(state_list[i]->state_control_0[new_state_index]); itr != values.end(); itr++){
						double add_transition_value = double(itr->second)*(1.0/double(NUM_TRANSITIONS));
						new_value += add_transition_value;
						break;
					}
				}

				// Action 1 transitions
				for (auto itr = R.find(state_list[i]); itr != R.end(); itr++){
					new_value += double(itr->second);
					break;
				}
				double old_value = 0;
				for (auto itr = values.find(state_list[i]); itr != values.end(); itr++){
					old_value = double(itr->second);
					break;
				}


				// std::cout<<old_value<<"-->"<<new_value<<std::endl;
				if(new_value != old_value){
					change = true;
					//update the map
					for (auto itr = values.find(state_list[i]); itr != values.end(); itr++){
						values.erase(itr);
						values.insert({state_list[i], new_value});	
						break;
					}
				}
			}
		}
		iteration+=1;
	}
	cout<<"Num iterations: "<<iteration<<endl;
	for (auto itr = values.find(state_list[0]); itr != values.end(); itr++){
		cout<<"Start Value: "<<itr->second<<endl;
		break;
	}


	OMPL_INFORM("%s: Starting planning with %u states already in datastructure", getName().c_str(), nn_->size());
	return {false, false};
}

void ompl::control::SMR::getPlannerData(base::PlannerData &data) const
{
	Planner::getPlannerData(data);

	std::vector<Node *> nodes;
	if (nn_)
		nn_->list(nodes);

	double delta = siC_->getPropagationStepSize();

	if (lastGoalNode_)
		data.addGoalVertex(base::PlannerDataVertex(lastGoalNode_->state));

	for (auto m : nodes)
	{
		if (m->parent)
		{
			if (data.hasControls())
				data.addEdge(base::PlannerDataVertex(m->parent->state), base::PlannerDataVertex(m->state),
							 control::PlannerDataEdgeControl(m->control, m->steps * delta));
			else
				data.addEdge(base::PlannerDataVertex(m->parent->state), base::PlannerDataVertex(m->state));
		}
		else
			data.addStartVertex(base::PlannerDataVertex(m->state));
	}
}