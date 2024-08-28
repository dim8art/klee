#![allow(non_snake_case)]

#[cfg(windows)]
use std::ptr::write_volatile;
use std::{path::PathBuf, ptr::write};
use std::io::{stdout, Write};

use std::{

    time::{Instant, Duration},

};

extern crate libafl;

#[cfg(feature = "tui")]
use libafl::monitors::tui::{ui::TuiUI, TuiMonitor};
#[cfg(not(feature = "tui"))]
use libafl::monitors::SimpleMonitor;
use libafl::{
    corpus::{InMemoryCorpus, OnDiskCorpus},
    events::SimpleEventManager,
    executors::{inprocess::InProcessExecutor, ExitKind},
    feedbacks::{MaxMapFeedback, CrashFeedback},
    fuzzer::{Fuzzer, StdFuzzer},
    generators::RandPrintablesGenerator,
    inputs::{BytesInput, HasTargetBytes},
    mutators::scheduled::{havoc_mutations, StdScheduledMutator},
    observers::StdMapObserver,
    schedulers::QueueScheduler,
    stages::mutational::StdMutationalStage,
    state::StdState,
};
use libafl_bolts::{current_nanos, rands::StdRand, tuples::tuple_list, AsSlice};

/// Coverage map with explicit assignments due to the lack of instrumentation
static mut SIGNALS: [u8; 16] = [0; 16];
static mut SIGNALS_PTR: *mut u8 = unsafe { SIGNALS.as_mut_ptr() };
static mut cnt : usize = 0;
fn signals_set(idx: usize) {
    unsafe { write(SIGNALS_PTR.add(idx), 1) };
}

#[repr(C)]
pub struct FuzzInfo {
    pub harness: extern "C" fn(*const u8, usize, u64),
    pub mainAddr : u64,
}

#[no_mangle]
pub unsafe extern "C" fn fuzzInternal(fi : FuzzInfo){
// The closure that we want to fuzz
let mut harness = |input: &BytesInput| {
    let fuzzing_started = Instant::now();
    let target = input.target_bytes();
    let buf = target.as_slice();
    (fi.harness)(buf.as_ptr(), buf.len(), fi.mainAddr);
    signals_set(cnt);
    cnt = cnt + 1;
    ExitKind::Ok
};

// Create an observation channel using the signals map
let observer = unsafe { StdMapObserver::from_mut_ptr("signals", SIGNALS_PTR, SIGNALS.len()) };

// Feedback to rate the interestingness of an input
let mut feedback = MaxMapFeedback::new(&observer);

// A feedback to choose if an input is a solution or not
let mut objective = CrashFeedback::new();

// create a State from scratchs
let mut state = StdState::new(
    // RNG
    StdRand::with_seed(153),
    // Corpus that will be evolved, we keep it in memory for performance
    InMemoryCorpus::new(),

    OnDiskCorpus::new(PathBuf::from("./crashes")).unwrap(),
    // States of the feedbacks.
    // The feedbacks can report the data that should persist in the State.
    &mut feedback,
    // Same for objective feedbacks
    &mut objective,
)
.unwrap();

// The Monitor trait define how the fuzzer stats are displayed to the user
#[cfg(not(feature = "tui"))]
let mon = SimpleMonitor::new(|s| println!("{s}"));
#[cfg(feature = "tui")]
let ui = TuiUI::with_version(String::from("Baby Fuzzer"), String::from("0.0.1"), false);
#[cfg(feature = "tui")]
let mon = TuiMonitor::new(ui);

// The event manager handle the various events generated during the fuzzing loop
// such as the notification of the addition of a new item to the corpus
let mut mgr = SimpleEventManager::new(mon);

// A queue policy to get testcasess from the corpus
let scheduler = QueueScheduler::new();

// A fuzzer with feedbacks and a corpus scheduler
let mut fuzzer = StdFuzzer::new(scheduler, feedback, objective);

// Create the executor for an in-process function with just one observer
let mut executor = InProcessExecutor::new(
    &mut harness,
    tuple_list!(observer),
    &mut fuzzer,
    &mut state,
    &mut mgr,
)
.expect("Failed to create the Executor");

// Generator of printable bytearrays of max size 32
let mut generator = RandPrintablesGenerator::new(32);

// Generate 8 initial inputs
state
    .generate_initial_inputs(&mut fuzzer, &mut executor, &mut generator, &mut mgr, 8)
    .expect("Failed to generate the initial corpus");

// Setup a mutational stage with a bassic bytes mutator
let mutator = StdScheduledMutator::new(havoc_mutations());
let mut stages = tuple_list!(StdMutationalStage::new(mutator));

let mut lock = stdout().lock();

for _x in 1..10 {
    write!(lock, "fuzzer is fuzzing").unwrap();

    fuzzer.fuzz_one(&mut stages, &mut executor,
        &mut state, &mut mgr).unwrap();

        //if fuzzing_started.elapsed() > timeout {
    //    break;
    //}
}

}

