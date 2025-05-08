import renderdoc as rd
import struct
import rdtest

class Groupshared(rdtest.TestCase):
    internal = True
    demos_test_name = None

    def check_support(self, **kwargs):
        # Only allow this if explicitly run
        if kwargs['test_include'] == self.demos_test_name:
            return True, ''
        return False, 'Disabled test'

    def check_compute_thread_result(self, test, action, x, y, z, dim, bufdata):
        try:
            real = struct.unpack_from("4f", bufdata, 16*x)

            workgroup = (0, 0, 0)
            trace = self.controller.DebugThread(workgroup, (x, y, z))

            _, variables = self.process_trace(trace)

            if trace.debugger is None:
                raise rdtest.TestFailureException(f"Test {test} at {action.eventId} got no debug result at {x},{y},{z}")

            # Find the source variable 'outval' at the highest instruction index
            name = 'outval'
            debugged = None
            countInst = len(trace.instInfo)
            for inst in range(countInst):
                sourceVars = trace.instInfo[countInst-1-inst].sourceVars
                try:
                    dataVars = [v for v in sourceVars if v.name == name]
                    if len(dataVars) == 0:
                        continue
                    debugged = self.evaluate_source_var(dataVars[0], variables)
                except KeyError as ex:
                    continue
                except rdtest.TestFailureException as ex:
                    continue
                break
            if debugged is None:
                raise rdtest.TestFailureException(f"Couldn't find source variable {name} at {x},{y},{z}")

            debuggedValue = list(debugged.value.f32v[0:4])

            if not rdtest.value_compare(real, debuggedValue, eps=5.0E-06):
                raise rdtest.TestFailureException(f"EID:{action.eventId} TID:{x},{y},{z} debugged thread value {debuggedValue} does not match output {real}")

        except rdtest.TestFailureException as ex:
            rdtest.log.error(f"Test {test} failed {ex}")
            return False
        finally:
            self.controller.FreeTrace(trace)

        return True

    def check_compute_tests(self, action):
        overallFailed = False
        tests = [a for a in action.children if a.flags & rd.ActionFlags.Dispatch]

        for test, action in enumerate(tests):
            failed = False
            self.controller.SetFrameEvent(action.eventId, False)

            pipe = self.controller.GetPipelineState()
            csrefl = pipe.GetShaderReflection(rd.ShaderStage.Compute)

            dim = csrefl.dispatchThreadsDimension

            rw = pipe.GetReadWriteResources(rd.ShaderStage.Compute)

            if len(rw) != 2:
                rdtest.log.error("Unexpected number of RW resources")
                return False

            outBuf = rw[1].descriptor.resource
            # each test writes up to one vec4 per thread * up to 64 threads
            maxThreads = 64
            dataPerThread = 4 * 4
            dataPerTest = dataPerThread * maxThreads
            bufdata = self.controller.GetBufferData(outBuf, test*dataPerTest, dataPerTest)

            for x in range(dim[0]):
                y = 0
                z = 0
                if not self.check_compute_thread_result(test, action, x, y, z, dim, bufdata):
                    failed = True

            overallFailed |= failed

            if not failed:
                rdtest.log.success(f"Tests at EID {action.eventId} successful")
            else:
                rdtest.log.error(f"Tests at EID {action.eventId} failed")

        return overallFailed

    def check_capture(self):
        overallFailed = False

        action = self.find_action("Compute Tests")
        sectionName = action.customName
        rdtest.log.begin_section(sectionName)
        overallFailed |= self.check_compute_tests(action)
        rdtest.log.end_section(sectionName)

        if overallFailed:
            raise rdtest.TestFailureException("Some tests were not as expected")

        self.check_renderdoc_log_asserts()

        rdtest.log.success("All tests matched")