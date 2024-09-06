#![allow(non_snake_case)]

use std::io::{stdout, Write};
#[cfg(windows)]
use std::ptr::write_volatile;
use std::{path::PathBuf, ptr::write};
use std::time::{Duration, Instant};
use std::mem::{transmute, size_of};

extern crate libafl;

#[cfg(feature = "tui")]
use libafl::monitors::tui::{ui::TuiUI, TuiMonitor};
#[cfg(not(feature = "tui"))]
use libafl::monitors::SimpleMonitor;
use libafl::{
    corpus::{Corpus, CorpusId, InMemoryCorpus, OnDiskCorpus},
    events::SimpleEventManager,
    executors::{inprocess::InProcessExecutor, ExitKind, InProcessForkExecutor},
    feedbacks::{CrashFeedback, MaxMapFeedback},
    fuzzer::{Fuzzer, StdFuzzer},
    generators::RandPrintablesGenerator,
    inputs::{BytesInput, HasMutatorBytes, HasTargetBytes},
    mutators::scheduled::{havoc_mutations, StdScheduledMutator},
    observers::StdMapObserver,
    schedulers::QueueScheduler,
    stages::mutational::StdMutationalStage,
    state::{HasSolutions, StdState},
};
use libafl_bolts::{
    current_nanos, prelude::StdShMemProvider, rands::StdRand, shmem::ShMemProvider,
    tuples::tuple_list, AsSlice, AsSliceMut,
};

/// Coverage map with explicit assignments due to the lack of instrumentation

static mut SOLUTIONS: Vec<FuzzSolution> = Vec::new();

#[repr(C)]
pub struct FuzzInfo {
    pub harness: extern "C" fn(*const u8, usize, u64, u32),
    pub mainAddr: u64,
}

#[repr(C)]
pub struct FuzzSolution {
    pub success: u8,
    pub numBytes: usize,
    pub bytes: *const u8,
}

#[no_mangle]
pub unsafe extern "C" fn getFuzzSolution() -> FuzzSolution {
    if SOLUTIONS.len() == 0 {
        let fs = FuzzSolution {
            success: 0,
            numBytes: 0,
            bytes: std::ptr::null_mut(),
        };
        return fs;
    }
    return SOLUTIONS.pop().unwrap();
}



#[no_mangle]
pub unsafe extern "C" fn fuzzInternal(fi: FuzzInfo) {
    let mut lock = stdout().lock();
    let mut shmem_provider   = StdShMemProvider::new().unwrap();
    let mut SIGNALS = shmem_provider.new_shmem(1024).unwrap();
    let SIGNALS_PTR: *mut u8 = SIGNALS.as_mut_ptr();
    let mut CNT_SHMEM = shmem_provider.new_shmem(1024).unwrap();
    let CNT_PTR = CNT_SHMEM.as_mut_ptr();
    let CNT =  unsafe { CNT_PTR as *mut u32};
    write!(lock, "*CNT = {}\n", *CNT).unwrap();
    let signals_set = |idx: usize| {
        unsafe { write(SIGNALS_PTR.add(idx), 1) };
    };
    //let mut shmem;
    //unsafe {
    //    shmem = StdShMemProvider::new()
    //       .unwrap()
    //       .new_shmem(MAX_EDGES_NUM)
    //        .unwrap();
    //}
    //let shmem_buf: &mut [u8] = shmem.as_slice_mut();
    //let EDGES_PTR: *mut u8 = shmem_buf.as_mut_ptr();

    // The closure that we want to fuzz
    let mut harness = |input: &BytesInput| {
        let target = input.target_bytes();
        let buf = target.as_slice();
        *CNT = *CNT + 1;
        write!(lock, "*CNT = {}\n", *CNT).unwrap();
        (fi.harness)(buf.as_ptr(), buf.len(), fi.mainAddr, *CNT);
        signals_set(*CNT as usize);
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
    let mut executor = InProcessForkExecutor::new(
        &mut harness,
        tuple_list!(observer),
        &mut fuzzer,
        &mut state,
        &mut mgr,
        core::time::Duration::from_millis(100000),
        shmem_provider,
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

    

    for _x in 1..10 {
        //write!(lock, "fuzzer is fuzzing").unwrap();

        fuzzer
            .fuzz_one(&mut stages, &mut executor, &mut state, &mut mgr)
            .unwrap();

        if state.solutions().count() > 0 {
            let solution = state.solutions().get(CorpusId(0)).unwrap();
            let bytes_iter = solution.borrow().clone();
            let bytes_iter = bytes_iter.input().as_ref().unwrap().bytes().iter();
            let mut numBytes: usize = 0;
            let solutionArray: *mut u8 = std::ptr::null_mut();
            for (pos, byte) in bytes_iter.enumerate() {
                numBytes += 1;
                solutionArray.add(pos).write(*byte);
                let fs = FuzzSolution {
                    success: 1,
                    numBytes: numBytes,
                    bytes: solutionArray,
                };
                SOLUTIONS.push(fs);
            }
        }
    }
    write!(lock, "solutionlen = {}\n", &SOLUTIONS.len()).unwrap();
    write!(lock, "fuzzer is done fuzzing\n").unwrap();
}
