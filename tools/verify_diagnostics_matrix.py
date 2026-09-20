from pathlib import Path
import subprocess, json, re
root=Path(__file__).resolve().parents[1]
evidence=root/'docs/validation/diagnostics_20260919'
evidence.mkdir(parents=True,exist_ok=True)
results=[]
for config in ['Debug','Release']:
    for mode in ['AUTO','ON','OFF']:
        directory=root/f'build/diagnostics-{config}-{mode}'
        expected=int(mode=='ON' or (mode=='AUTO' and config=='Debug'))
        run=subprocess.run([str(directory/'tests/qlog_producer_diagnostics_test')],capture_output=True,text=True)
        assert run.returncode==0,run.stderr
        assert f'diagnostics={expected};' in run.stdout,run.stdout
        symbols=subprocess.check_output(['nm','-C',str(directory/'libqlog.a')],text=True)
        present='qlog::AsyncLogger::record_call_result(' in symbols
        assert present==bool(expected),(config,mode,'library bridge presence')
        command=['g++','-std=c++20',f'-DQLOG_ENABLE_DIAGNOSTICS={expected}',f'-DQLOG_TEST_EXPECT_DIAGNOSTICS={expected}',
                 '-I'+str(root/'include'),'-E','-P',str(root/'tests/producer_diagnostics_consumer.cpp')]
        preprocessed=subprocess.check_output(command,text=True)
        assert ('record_call_result' in preprocessed)==bool(expected)
        consumer=directory/'tests/CMakeFiles/qlog_producer_diagnostics_test.dir/producer_diagnostics_consumer.cpp.o'
        consumer_symbols=subprocess.check_output(['nm','-C',str(consumer)],text=True)
        assert ('record_call_result' in consumer_symbols)==bool(expected)
        (evidence/f'{config}-{mode}-runtime.txt').write_text(run.stdout)
        log=subprocess.check_output(['ctest','--test-dir',str(directory),'-L','diagnostics','--output-on-failure'],text=True)
        assert '100% tests passed, 0 tests failed out of 2' in log
        (evidence/f'{config}-{mode}-ctest.log').write_text(log)
        results.append(dict(config=config,mode=mode,expected=expected,library_bridge=present,
                            consumer_bridge_reference=bool(expected),preprocessed_bridge=bool(expected)))
bad=subprocess.run(['cmake','-S',str(root),'-B',str(root/'build/diagnostics-invalid-option'),
                    '-DQLOG_ENABLE_DIAGNOSTICS=INVALID','-DBUILD_TESTING=OFF'],capture_output=True,text=True)
assert bad.returncode!=0 and 'QLOG_ENABLE_DIAGNOSTICS must be AUTO, ON or OFF' in bad.stdout+bad.stderr
(evidence/'invalid-option.txt').write_text(bad.stdout+bad.stderr)
(evidence/'macro-and-symbol-checks.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results,indent=2))
print('Six configurations, independent mode expectations, library/consumer symbols, preprocessing and invalid-option rejection passed')
