#if !defined(Lane_h)
#define Lane_h

#include <string>
#include <vector>
#include <atomic>
#include <utility>
#include <optional>

#include "tbb/task_group.h"

#include "Source.h"
#include "SerializerWrapper.h"
#include "OutputerBase.h"
#include "Waiter.h"
#include "EventAuxReader.h"
#include "FunctorTask.h"

class Lane {
public:
 Lane(std::string const& iFileName, double iScaleFactor): source_(iFileName) {
    
    const std::string eventAuxiliaryBranchName{"EventAuxiliary"}; 
    serializers_.reserve(source_.branches().size());
    waiters_.reserve(source_.branches().size());
    for( int ib = 0; ib< source_.branches().size(); ++ib) {
      auto b = source_.branches()[ib];
      auto address = reinterpret_cast<void**>(b->GetAddress());
      if(eventAuxiliaryBranchName == b->GetName()) {
	eventAuxReader_ = EventAuxReader(address);
      }
      
      serializers_.emplace_back(b->GetName(), address,source_.classForEachBranch()[ib]);
      waiters_.emplace_back(ib, iScaleFactor);
    }
  }

  void processEventsAsync(std::atomic<long>& index, tbb::task_group& group, const OutputerBase& outputer) {
    doNextEvent(index, group,  outputer);
  }

  std::vector<SerializerWrapper> const& serializers() const { return serializers_;}

  std::chrono::microseconds sourceAccumulatedTime() const { return source_.accumulatedTime(); }
private:

  void processEventAsync(tbb::task_group& group, TaskHolder iCallback, const OutputerBase& outputer) { 
    //std::cout <<"make process event task"<<std::endl;
    TaskHolder holder(group, 
		      make_functor_task([&outputer, this, callback=std::move(iCallback)]() {
			  outputer.outputAsync(eventAuxReader_->doWork(),
					       serializers_, std::move(callback));
			}));
    
    size_t index=0;
    for(auto& s: serializers_) {
      auto* waiter = &waiters_[index];
      TaskHolder waitH(group,
		       make_functor_task([holder,waiter,this]() {
			   waiter->waitAsync(serializers_,std::move(holder));
			 }));
      s.doWorkAsync(group, waitH);
      ++index;
    }
  }

  void doNextEvent(std::atomic<long>& index, tbb::task_group& group,  const OutputerBase& outputer) {
    using namespace std::string_literals;
    auto i = ++index;
    i-=1;
    if(source_.gotoEvent(i)) {
      std::cout <<"event "+std::to_string(i)+"\n"<<std::flush;
      
      //std::cout <<"make doNextEvent task"<<std::endl;
      TaskHolder recursiveTask(group, make_functor_task([this, &index, &group, &outputer]() {
	    doNextEvent(index, group, outputer);
	  }));
      processEventAsync(group, std::move(recursiveTask), outputer);
    }
  }

  Source source_;
  std::vector<SerializerWrapper> serializers_;
  std::vector<Waiter> waiters_;
  std::optional<EventAuxReader> eventAuxReader_;
};

#endif
