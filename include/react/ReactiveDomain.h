#pragma once

#include <atomic>
#include <functional>
#include <memory>

#include "tbb/concurrent_vector.h"
#include "tbb/queuing_mutex.h"
#include "tbb/spin_mutex.h"

#include "Observer.h"

#include "react/common/Types.h"
#include "react/logging/EventLog.h"
#include "react/logging/EventRecords.h"

////////////////////////////////////////////////////////////////////////////////////////
namespace react {

template <typename D, typename S>
class RSignal;

template <typename D, typename S>
class RVarSignal;

template <typename D, typename E>
class REvents;

template <typename D, typename E>
class REventSource;

enum class EventToken;

template
<
	typename D,
	typename TFunc,
	typename ... TArgs
>
inline auto MakeSignal(TFunc func, const RSignal<D,TArgs>& ... args)
	-> RSignal<D,decltype(func(args() ...))>;

//
//template <typename TTurnInterface>
//class TransactionData
//{
//public:
//	TransactionData() :
//		id_{ INT_MIN },
//		curInputPtr_{ &input1_},
//		nextInputPtr_{ &input2_ }
//	{
//	}
//
//	int		Id() const		{ return id_; }
//	void	SetId(int id)	{ id_ = id; }
//
//	tbb::concurrent_vector<IObserverNode*>	DetachedObservers;
//
//	TransactionInput<TTurnInterface>& Input()		{ return *curInputPtr_; }
//	TransactionInput<TTurnInterface>& NextInput()	{ return *nextInputPtr_; }
//
//	bool ContinueTransaction()
//	{
//		if (nextInputPtr_->IsEmpty())
//			return false;
//
//		id_ = INT_MIN;
//		DetachedObservers.clear();
//
//		std::swap(curInputPtr_, nextInputPtr_);
//		nextInputPtr_->Reset();
//		return true;
//	}
//
//private:
//	int		id_;
//
//	TransactionInput<TTurnInterface>*	curInputPtr_;
//	TransactionInput<TTurnInterface>*	nextInputPtr_;
//
//	TransactionInput<TTurnInterface>	input1_;
//	TransactionInput<TTurnInterface>	input2_;
//};

////////////////////////////////////////////////////////////////////////////////////////
/// ContinuationInput
////////////////////////////////////////////////////////////////////////////////////////
class ContinuationInput
{
public:
	typedef std::function<void()>	InputClosureT;

	inline bool IsEmpty() const { return bufferedInputs_.size() == 0; }

	template <typename F>
	void Add(F&& input)
	{
		bufferedInputs_.push_back(std::forward<F>(input));
	}

	inline void Execute()
	{
		for (auto f : bufferedInputs_)
			f();
		bufferedInputs_.clear();
	}

private:
	tbb::concurrent_vector<InputClosureT>	bufferedInputs_;
};

////////////////////////////////////////////////////////////////////////////////////////
/// CommitFlags
////////////////////////////////////////////////////////////////////////////////////////
enum ETurnFlags
{
	enable_input_merging	= 1 << 0
};

////////////////////////////////////////////////////////////////////////////////////////
/// EngineInterface
////////////////////////////////////////////////////////////////////////////////////////
template
<
	typename D,
	typename TEngine
>
struct EngineInterface
{
	typedef typename TEngine::NodeInterface		NodeInterface;
	typedef typename TEngine::TurnInterface		TurnInterface;

	static TEngine& Engine()
	{
		static TEngine engine;
		return engine;
	}

	static void OnNodeCreate(NodeInterface& node)
	{
		D::Log().template Append<NodeCreateEvent>(GetObjectId(node), node.GetNodeType());
		Engine().OnNodeCreate(node);
	}

	static void OnNodeDestroy(NodeInterface& node)
	{
		D::Log().template Append<NodeDestroyEvent>(GetObjectId(node));
		Engine().OnNodeDestroy(node);
	}

	static void OnNodeAttach(NodeInterface& node, NodeInterface& parent)
	{
		D::Log().template Append<NodeAttachEvent>(GetObjectId(node), GetObjectId(parent));
		Engine().OnNodeAttach(node, parent);
	}

	static void OnNodeDetach(NodeInterface& node, NodeInterface& parent)
	{
		D::Log().template Append<NodeDetachEvent>(GetObjectId(node), GetObjectId(parent));
		Engine().OnNodeDetach(node, parent);
	}

	static void OnNodePulse(NodeInterface& node, TurnInterface& turn)
	{
		D::Log().template Append<NodePulseEvent>(GetObjectId(node), turn.Id());
		Engine().OnNodePulse(node, turn);
	}

	static void OnNodeIdlePulse(NodeInterface& node, TurnInterface& turn)
	{
		D::Log().template Append<NodeIdlePulseEvent>(GetObjectId(node), turn.Id());
		Engine().OnNodeIdlePulse(node, turn);
	}

	static void OnNodeShift(NodeInterface& node, NodeInterface& oldParent, NodeInterface& newParent, TurnInterface& turn)
	{
		D::Log().template Append<NodeInvalidateEvent>(GetObjectId(node), GetObjectId(oldParent), GetObjectId(newParent), turn.Id());
		Engine().OnNodeShift(node, oldParent, newParent, turn);
	}

	static void OnTurnAdmissionStart(TurnInterface& turn)
	{
		Engine().OnTurnAdmissionStart(turn);
	}

	static void OnTurnAdmissionEnd(TurnInterface& turn)
	{
		Engine().OnTurnAdmissionEnd(turn);
	}

	static void OnTurnInputChange(NodeInterface& node, TurnInterface& turn)
	{
		D::Log().template Append<InputNodeAdmissionEvent>(GetObjectId(node), turn.Id());
		Engine().OnTurnInputChange(node, turn);
	}

	static void OnTurnPropagate(TurnInterface& turn)
	{
		D::Log().template Append<TransactionBeginEvent>(turn.Id());
		Engine().OnTurnPropagate(turn);
		D::Log().template Append<TransactionEndEvent>(turn.Id());
	}

	template <typename F>
	static bool TryMerge(F&& f)
	{
		return Engine().TryMerge(std::forward<F>(f));
	}
};

////////////////////////////////////////////////////////////////////////////////////////
/// Domain
////////////////////////////////////////////////////////////////////////////////////////
template
<
	typename TEngine,
	typename TLog = NullLog
>
struct DomainPolicy
{
	using Engine	= TEngine;
	using Log		= TLog;
};

using TurnIdT = uint;
using TurnFlagsT = uint;

template <typename D, typename TPolicy>
class DomainBase
{
public:
	using TurnT = typename TPolicy::Engine::TurnInterface;

	DomainBase() = delete;

	using Policy = TPolicy;
	using Engine = EngineInterface<D, typename Policy::Engine>;

	////////////////////////////////////////////////////////////////////////////////////////
	/// Aliases for reactives of current domain
	////////////////////////////////////////////////////////////////////////////////////////
	template <typename S>
	using Signal = RSignal<D,S>;

	template <typename S>
	using VarSignal = RVarSignal<D,S>;

	template <typename E = EventToken>
	using Events = REvents<D,E>;

	template <typename E = EventToken>
	using EventSource = REventSource<D,E>;

	using Observer = RObserver<D>;

	////////////////////////////////////////////////////////////////////////////////////////
	/// ObserverRegistry
	////////////////////////////////////////////////////////////////////////////////////////
	static ObserverRegistry<D>& Observers()
	{
		static ObserverRegistry<D> registry;
		return registry;
	}

	////////////////////////////////////////////////////////////////////////////////////////
	/// Log
	////////////////////////////////////////////////////////////////////////////////////////
	static typename Policy::Log& Log()
	{
		static Policy::Log log;
		return log;
	}

	////////////////////////////////////////////////////////////////////////////////////////
	/// MakeVar (higher order signal)
	////////////////////////////////////////////////////////////////////////////////////////
	template
	<
		template <typename Domain_, typename Val_> class TOuter,
		typename TInner
	>
	static inline auto MakeVar(const TOuter<D,TInner>& value)
		-> VarSignal<Signal<TInner>>
	{
		return react::MakeVar<D>(value);
	}

	////////////////////////////////////////////////////////////////////////////////////////
	/// MakeVar
	////////////////////////////////////////////////////////////////////////////////////////
	template <typename S>
	static inline auto MakeVar(const S& value)
		-> VarSignal<S>
	{
		return react::MakeVar<D>(value);
	}

	////////////////////////////////////////////////////////////////////////////////////////
	/// MakeVal
	////////////////////////////////////////////////////////////////////////////////////////
	template <typename S>
	static inline auto MakeVal(const S& value)
		-> Signal<S>
	{
		return react::MakeVal<D>(value);
	}

	////////////////////////////////////////////////////////////////////////////////////////
	/// MakeSignal
	////////////////////////////////////////////////////////////////////////////////////////
	template
	<
		typename TFunc,
		typename ... TArgs
	>
	static inline auto MakeSignal(TFunc func, const Signal<TArgs>& ... args)
		-> Signal<decltype(func(args() ...))>
	{
		typedef decltype(func(args() ...)) S;

		return react::MakeSignal<D>(func, args ...);
	}

	////////////////////////////////////////////////////////////////////////////////////////
	/// MakeEventSource
	////////////////////////////////////////////////////////////////////////////////////////
	template <typename E>
	static inline auto MakeEventSource()
		-> EventSource<E>
	{
		return react::MakeEventSource<D,E>();
	}

	static inline auto MakeEventSource()
		-> EventSource<EventToken>
	{
		return react::MakeEventSource<D>();
	}

	//////////////////////////////////////////////////////////////////////////////////////////
	///// Transaction
	//////////////////////////////////////////////////////////////////////////////////////////
	//class Transaction
	//{
	//public:
	//	Transaction() :
	//		flags_{ defaultCommitFlags_ }
	//	{
	//	}

	//	Transaction(int flags) :
	//		flags_{ flags }
	//	{
	//	}

	//	void Init()
	//	{
	//	}

	//	template <typename R, typename V>
	//	void AddSignalInput(R& r, const V& v)
	//	{
	//		r.SetNewValue(v);
	//		changedSignals_.push_back(&r);
	//	}

	//	void Commit()
	//	{
	//		REACT_ASSERT(committed_ == false, "Transaction already committed.");
	//		committed_ = true;

	//		for (auto* p : changedSignals_)
	//			if (r.ApplyNewValue())
	//				Engine::OnTurnInputChange(*p, turn);

	//		Engine::OnTurnCommit(data_);

	//		do
	//		{
	//			data_.SetId(nextTransactionId());
	//			data_.Input().SetFlags(flags_);
	//			Engine::OnTransactionCommit(data_);

	//			// Apply detachments requested by DetachThisObserver
	//			for (auto* o : data_.DetachedObservers)
	//				Observers().Unregister(o);
	//		}
	//		while (data_.ContinueTransaction());
	//	}

	//	TransactionData& Data()
	//	{
	//		return data_;
	//	}

	//private:
	//	static std::vector<IReactiveNode*>	changedInputs_;

	//	int		flags_;
	//	bool	committed_ = false;

	//	TransactionData		data_;
	//};

	////////////////////////////////////////////////////////////////////////////////////////
	/// DoTransaction
	////////////////////////////////////////////////////////////////////////////////////////
	template <typename F>
	static void DoTransaction(F&& func)
	{
		DoTransaction(std::forward<F>(func), turnFlags_);
	}

	template <typename F>
	static void DoTransaction(F&& func, TurnFlagsT flags)
	{
		// Attempt to add input to another turn.
		// If successful, blocks until other turn is done and returns.
		if (Engine::TryMerge(std::forward<F>(func)))
			return;

		bool shouldPropagate = false;

		auto turn = makeTurn(flags);

		// Phase 1 - Input admission
		transactionState_.active = true;
		Engine::OnTurnAdmissionStart(turn);
		func();
		Engine::OnTurnAdmissionEnd(turn);
		transactionState_.active = false;

		// Phase 2 - Apply input node changes
		for (auto* p : transactionState_.inputs)
			if (p->Tick(&turn) == ETickResult::pulsed)
				shouldPropagate = true;
		transactionState_.inputs.clear();

		// Phase 3 - Propagate changes
		if (shouldPropagate)
			Engine::OnTurnPropagate(turn);

		postProcessTurn(turn);
	}

	////////////////////////////////////////////////////////////////////////////////////////
	/// AddInput
	////////////////////////////////////////////////////////////////////////////////////////
	template <typename R, typename V>
	static void AddInput(R&& r, V&& v)
	{
		if (! ContinuationHolder_::IsNull())
		{
			addContinuationInput(std::forward<R>(r), std::forward<V>(v));
		}
		else if (transactionState_.active)
		{
			addTransactionInput(std::forward<R>(r), std::forward<V>(v));
		}
		else
		{
			addSimpleInput(std::forward<R>(r), std::forward<V>(v));
		}
	}

	////////////////////////////////////////////////////////////////////////////////////////
	/// Set/Clear continuation
	////////////////////////////////////////////////////////////////////////////////////////
	static void SetCurrentContinuation(TurnT& turn)
	{
		ContinuationHolder_::Set(&turn.continuation_);
	}

	static void ClearCurrentContinuation()
	{
		ContinuationHolder_::Reset();
	}

	////////////////////////////////////////////////////////////////////////////////////////
	/// Options
	////////////////////////////////////////////////////////////////////////////////////////
	template <typename Opt>
	static void Set(int v)		{ static_assert(false, "Set option not implemented."); }

	template <typename Opt>
	static bool IsSet(int v)	{ static_assert(false, "IsSet option not implemented."); }

	template <typename Opt>
	static void Unset(int v)	{ static_assert(false, "Unset option not implemented."); }	

	template <typename Opt>
	static void Reset()			{ static_assert(false, "Reset option not implemented."); }

	template <> static void Set<ETurnFlags>(int v)		{ turnFlags_ |= v; }
	template <> static bool IsSet<ETurnFlags>(int v)	{ return (turnFlags_ & v) != 0 }
	template <> static void Unset<ETurnFlags>(int v)	{ turnFlags_ &= ~v;}
	template <> static void Reset<ETurnFlags>()			{ turnFlags_ = 0;}

private:

	////////////////////////////////////////////////////////////////////////////////////////
	/// Transaction input continuation
	////////////////////////////////////////////////////////////////////////////////////////
	struct ContinuationHolder_ : public ThreadLocalStaticPtr<ContinuationInput> {};

	static __declspec(thread) TurnFlagsT turnFlags_;

	static std::atomic<TurnIdT> nextTurnId_;

	static TurnIdT nextTurnId()
	{
		auto curId = nextTurnId_.fetch_add(1, std::memory_order_relaxed);

		if (curId == INT_MAX)
			nextTurnId_.fetch_sub(INT_MAX);

		return curId;
	}

	struct TransactionState
	{
		bool	active = false;
		std::vector<IReactiveNode*>	inputs;
	};

	static TransactionState transactionState_;

	static TurnT makeTurn(TurnFlagsT flags)
	{
		return TurnT(nextTurnId(), flags);
	}

	// Create a turn with a single input
	template <typename R, typename V>
	static void addSimpleInput(R&& r, V&& v)
	{
		auto turn = makeTurn(0);

		Engine::OnTurnAdmissionStart(turn);
		r.AddInput(std::forward<V>(v));
		Engine::OnTurnAdmissionEnd(turn);

		if (r.Tick(&turn) == ETickResult::pulsed)
			Engine::OnTurnPropagate(turn);

		postProcessTurn(turn);
	}

	// This input is part of an active transaction
	template <typename R, typename V>
	static void addTransactionInput(R&& r, V&& v)
	{
		r.AddInput(std::forward<V>(v));
		transactionState_.inputs.push_back(&r);
	}

	// Input happened during a turn - buffer in continuation
	template <typename R, typename V>
	static void addContinuationInput(R&& r, V&& v)
	{
		// Copy v
		ContinuationHolder_::Get()->Add(
			[&r,v] { addTransactionInput(r, std::move(v)); }
		);
	}


	static void postProcessTurn(TurnT& turn)
	{
		for (auto* o : turn.detachedObservers_)
			Observers().Unregister(o);

		// Steal continuation from current turn
		if (! turn.continuation_.IsEmpty())
			processContinuations(std::move(turn.continuation_), 0);
	}

	static void processContinuations(ContinuationInput cont, TurnFlagsT flags)
	{
		// No merging for continuations
		flags &= ~enable_input_merging;

		while (true)
		{
			bool shouldPropagate = false;
			auto turn = makeTurn(flags);

			transactionState_.active = true;
			Engine::OnTurnAdmissionStart(turn);
			cont.Execute();
			Engine::OnTurnAdmissionEnd(turn);
			transactionState_.active = false;

			for (auto* p : transactionState_.inputs)
				if (p->Tick(&turn) == ETickResult::pulsed)
					shouldPropagate = true;
			transactionState_.inputs.clear();

			if (shouldPropagate)
				Engine::OnTurnPropagate(turn);

			for (auto* o : turn.detachedObservers_)
				Observers().Unregister(o);

			if (turn.continuation_.IsEmpty())
				break;

			cont = std::move(turn.continuation_);
		}
		
	}
};

template <typename D, typename TPolicy>
std::atomic<TurnIdT> DomainBase<D,TPolicy>::nextTurnId_( 0 );

template <typename D, typename TPolicy>
TurnFlagsT DomainBase<D,TPolicy>::turnFlags_( 0 );

template <typename D, typename TPolicy>
typename DomainBase<D,TPolicy>::TransactionState DomainBase<D,TPolicy>::transactionState_;

////////////////////////////////////////////////////////////////////////////////////////
/// Ensure singletons are created immediately after domain declaration (TODO hax)
////////////////////////////////////////////////////////////////////////////////////////
namespace impl
{

template <typename D>
class DomainInitializer
{
public:
	DomainInitializer()
	{
		D::Log();
		typename D::Engine::Engine();
	}
};

} // ~namespace react::impl

#define REACTIVE_DOMAIN(name, ...) \
	struct name : public react::DomainBase<name, react::DomainPolicy<__VA_ARGS__ >> {}; \
	react::impl::DomainInitializer< name > name ## _initializer_;

} // ~namespace react