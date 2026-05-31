package com.libreshockwave.vm.opcode;

import com.libreshockwave.chunks.ScriptChunk;
import com.libreshockwave.id.ChunkId;
import com.libreshockwave.lingo.Opcode;
import com.libreshockwave.vm.LingoVM;
import com.libreshockwave.vm.Scope;
import com.libreshockwave.vm.builtin.BuiltinRegistry;
import com.libreshockwave.vm.builtin.movie.MoviePropertyProvider;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.xtra.Xtra;
import com.libreshockwave.vm.xtra.XtraManager;
import com.libreshockwave.vm.builtin.xtra.XtraBuiltins;
import org.junit.jupiter.api.Test;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicInteger;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class CallOpcodesReceiverStyleExtCallTest {

    @Test
    void extCallDispatchesReceiverStyleXtraHandlers() {
        BuiltinRegistry builtins = new BuiltinRegistry();
        XtraManager manager = new XtraManager();
        AtomicInteger calls = new AtomicInteger();
        manager.registerXtra(new Xtra() {
            @Override
            public String getName() {
                return "TestXtra";
            }

            @Override
            public int createInstance(List<Datum> args) {
                return 1;
            }

            @Override
            public void destroyInstance(int instanceId) {
            }

            @Override
            public Datum callHandler(int instanceId, String handlerName, List<Datum> args) {
                calls.incrementAndGet();
                assertEquals("#1", handlerName);
                assertEquals("payload", args.get(0).toStr());
                return Datum.of("xtra-ok");
            }

            @Override
            public Datum getProperty(int instanceId, String propertyName) {
                return Datum.VOID;
            }

            @Override
            public void setProperty(int instanceId, String propertyName, Datum value) {
            }
        });

        ExecutionContext ctx = createContext(1, builtins);
        ctx.push(new Datum.ArgList(List.of(new Datum.XtraInstance("TestXtra", 1), Datum.of("payload"))));

        XtraBuiltins.setManager(manager);
        try {
            boolean advance = invokeExtCall(ctx);

            assertTrue(advance);
            assertEquals(1, calls.get());
            assertEquals("xtra-ok", ctx.pop().toStr());
        } finally {
            XtraBuiltins.clearManager();
        }
    }

    @Test
    void labelBuiltinResolvesMovieFrameLabels() {
        BuiltinRegistry builtins = new BuiltinRegistry();
        MoviePropertyProvider provider = new MoviePropertyProvider() {
            @Override
            public Datum getMovieProp(String propName) {
                return Datum.VOID;
            }

            @Override
            public boolean setMovieProp(String propName, Datum value) {
                return false;
            }

            @Override
            public int getFrameForLabel(String label) {
                return "connectloop".equalsIgnoreCase(label) ? 27 : 0;
            }
        };

        MoviePropertyProvider.setProvider(provider);
        try {
            assertEquals(27, builtins.invoke("label", new LingoVM(null), List.of(Datum.of("connectloop"))).toInt());
            assertEquals(27, builtins.invoke("label", new LingoVM(null),
                    List.of(Datum.MOVIE, Datum.of("connectloop"))).toInt());
        } finally {
            MoviePropertyProvider.clearProvider();
        }
    }

    private static boolean invokeExtCall(ExecutionContext ctx) {
        try {
            Method method = CallOpcodes.class.getDeclaredMethod("extCall", ExecutionContext.class);
            method.setAccessible(true);
            return (boolean) method.invoke(null, ctx);
        } catch (NoSuchMethodException | IllegalAccessException e) {
            throw new AssertionError(e);
        } catch (InvocationTargetException e) {
            Throwable cause = e.getCause();
            if (cause instanceof RuntimeException runtimeException) {
                throw runtimeException;
            }
            if (cause instanceof Error error) {
                throw error;
            }
            throw new RuntimeException(cause);
        }
    }

    private static ExecutionContext createContext(int argument, BuiltinRegistry builtins) {
        ScriptChunk.Handler.Instruction instruction = new ScriptChunk.Handler.Instruction(
                0,
                Opcode.EXT_CALL,
                0,
                argument);
        ScriptChunk.Handler handler = new ScriptChunk.Handler(
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                List.of(),
                List.of(),
                List.of(instruction),
                Map.of(0, 0));
        ScriptChunk script = new ScriptChunk(
                null,
                new ChunkId(1),
                ScriptChunk.ScriptType.MOVIE_SCRIPT,
                0,
                List.of(handler),
                List.of(),
                List.of(),
                List.of(),
                new byte[0]);
        Scope scope = new Scope(script, handler, List.of(), Datum.VOID);
        return new ExecutionContext(
                scope,
                instruction,
                builtins,
                null,
                (scriptChunk, targetHandler, args, receiver) -> Datum.VOID,
                name -> null,
                new ExecutionContext.GlobalAccessor() {
                    @Override
                    public Datum getGlobal(String name) {
                        return Datum.VOID;
                    }

                    @Override
                    public void setGlobal(String name, Datum value) {
                    }
                },
                (name, args) -> builtins.invoke(name, new LingoVM(null), args),
                errorState -> {
                },
                () -> "(test)");
    }
}
