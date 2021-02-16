/* Copyright (c) 2021 Guilherme Andrade
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy  of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/* ------------------------------------------------------------------
 * Includes
 * ------------------------------------------------------------------ */

#include <assert.h>
#include <string.h>

#include "erl_nif.h"

/* ------------------------------------------------------------------
 * NIF Function Declarations
 * ------------------------------------------------------------------ */

static ERL_NIF_TERM nif_monitor(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]);
static ERL_NIF_TERM nif_demonitor(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]);

/* ------------------------------------------------------------------
 * NIF Resource Declarations
 * ------------------------------------------------------------------ */

#define SEMANTIC_MONITOR_RESOURCE_NAME "vaktari.semantic_monitor"

static ErlNifResourceType* semantic_monitor_resource_type = NULL;

typedef struct {
    ErlNifMonitor monitor; // this also acts as an atomic flag to decide who frees `env`
    ErlNifPid monitorer_pid;
    ErlNifEnv* env;
    ERL_NIF_TERM self_reference; // prevent the GC from destroying us too soon
    ERL_NIF_TERM down_data;
} SemanticMonitor;

static void init_semantic_monitor_resource_type(ErlNifEnv* env);

static int get_semantic_monitor(ErlNifEnv* env, ERL_NIF_TERM term, SemanticMonitor** vaktari);

static void handle_triggered_semantic_monitor_DOWN(ErlNifEnv* env, void* resource,
                                                   ErlNifPid* pid, ErlNifMonitor* mon);

static void delete_semantic_monitor_resource(ErlNifEnv* env, void* resource);

/* ------------------------------------------------------------------
 * Helper Declarations
 * ------------------------------------------------------------------ */

static struct {
    ERL_NIF_TERM _DOWN;
    ERL_NIF_TERM _false;
    ERL_NIF_TERM _true;
    ERL_NIF_TERM badarg;
    ERL_NIF_TERM noproc;
    ERL_NIF_TERM notsup;
    ERL_NIF_TERM process;
    ERL_NIF_TERM undefined;
} Atoms;

static void init_atoms(ErlNifEnv* env);

static ERL_NIF_TERM make_DOWN(ErlNifEnv* env, ERL_NIF_TERM monitor_term,
                              ErlNifPid* pid, ERL_NIF_TERM reason,
                              ERL_NIF_TERM down_data);
static ERL_NIF_TERM raise_badarg(ErlNifEnv* env, ERL_NIF_TERM term);
static ERL_NIF_TERM raise_notsup(ErlNifEnv* env);

/* ------------------------------------------------------------------
 * Hack Definitions
 * ------------------------------------------------------------------ */

#define UNUSED(x) (void)(x)

/* ------------------------------------------------------------------
 * NIF Initialization
 * ------------------------------------------------------------------ */

static ErlNifFunc nif_funcs[] = {
    {"monitor", 2, nif_monitor, 0},
    {"demonitor", 1, nif_demonitor, 0}
};

static int load_nif(ErlNifEnv* env, void** priv, ERL_NIF_TERM load_info) {
    UNUSED(priv);
    UNUSED(load_info);
    init_semantic_monitor_resource_type(env);
    init_atoms(env);
    return 0;
}

ERL_NIF_INIT(vaktari_nif, nif_funcs, load_nif, NULL, NULL, NULL)

/* ------------------------------------------------------------------
 * NIF Function Definitions
 * ------------------------------------------------------------------ */

static ERL_NIF_TERM nif_monitor(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
    ErlNifPid monitored_pid;
    ERL_NIF_TERM down_data;
    ErlNifPid self;

    if (argc != 2) {
        return raise_notsup(env);
    }

    if (!enif_get_local_pid(env, argv[0], &monitored_pid)) {
        return raise_badarg(env, argv[0]);
    }
    down_data = argv[1];

    if (!enif_self(env, &self)) {
        return raise_notsup(env);
    }

    void* semantic_monitor_resource = enif_alloc_resource(semantic_monitor_resource_type,
                                                          sizeof(SemanticMonitor));
    SemanticMonitor* semantic_monitor = (SemanticMonitor*) semantic_monitor_resource;
    memset(semantic_monitor, 0, sizeof(SemanticMonitor));

    if (enif_monitor_process(env, semantic_monitor_resource, &monitored_pid,
                             &semantic_monitor->monitor) != 0)
    {
        enif_release_resource(semantic_monitor_resource);
        ERL_NIF_TERM simulated_ref = enif_make_ref(env);
        ERL_NIF_TERM down_msg = make_DOWN(env,
                                          simulated_ref,
                                          &monitored_pid,
                                          Atoms.noproc,
                                          down_data);
        enif_send(env, &self, NULL, down_msg);
        return simulated_ref;
    }

    ERL_NIF_TERM semantic_monitor_term = enif_make_resource(env, semantic_monitor_resource);
    enif_release_resource(semantic_monitor_resource);

    semantic_monitor->monitorer_pid = self;
    semantic_monitor->env = enif_alloc_env();
    semantic_monitor->self_reference = enif_make_copy(semantic_monitor->env,
                                                      semantic_monitor_term);
    semantic_monitor->down_data = enif_make_copy(semantic_monitor->env, down_data);

    return semantic_monitor_term;
}

static ERL_NIF_TERM nif_demonitor(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
    SemanticMonitor* semantic_monitor;
    ErlNifPid self;

    if (argc != 1) {
        return raise_notsup(env);
    }

    if (!get_semantic_monitor(env, argv[0], &semantic_monitor)) {
        if (enif_is_ref(env, argv[0])) {
            // assume this is a `simulated_ref` as produced in `nif_monitor`
            return Atoms._false;
        }
        return raise_badarg(env, argv[0]);
    }

    if (!enif_self(env, &self)) {
        return raise_notsup(env);
    }
    else if (enif_compare_pids(&self, &semantic_monitor->monitorer_pid) != 0) {
        return Atoms._false;
    }

    void* semantic_monitor_resource = (void*) semantic_monitor;
    if (enif_demonitor_process(env, semantic_monitor_resource,
                               &semantic_monitor->monitor) == 0)
    {
        enif_free_env(semantic_monitor->env);
        semantic_monitor->env = NULL;
        return Atoms._true;
    }
    return Atoms._false;
}

/* ------------------------------------------------------------------
 * NIF Resource Definitions
 * ------------------------------------------------------------------ */

static void init_semantic_monitor_resource_type(ErlNifEnv* env) {
    ErlNifResourceTypeInit resource_callbacks;
    memset(&resource_callbacks, 0, sizeof(resource_callbacks));

    resource_callbacks.dtor = delete_semantic_monitor_resource;
    resource_callbacks.stop = NULL;
    resource_callbacks.down = handle_triggered_semantic_monitor_DOWN;

    semantic_monitor_resource_type = enif_open_resource_type_x(
            env,
            SEMANTIC_MONITOR_RESOURCE_NAME,
            &resource_callbacks,
            ERL_NIF_RT_CREATE, NULL);
}

static inline int get_semantic_monitor(ErlNifEnv* env, ERL_NIF_TERM term, SemanticMonitor** vaktari) {
    return enif_get_resource(env, term, semantic_monitor_resource_type, (void **) vaktari);
}

static void handle_triggered_semantic_monitor_DOWN(ErlNifEnv* env, void* resource,
                                                   ErlNifPid* pid, ErlNifMonitor* mon) {
    UNUSED(env);
    UNUSED(pid);
    UNUSED(mon);
    SemanticMonitor* semantic_monitor = (SemanticMonitor*) resource;

    ERL_NIF_TERM down_msg = make_DOWN(semantic_monitor->env,
                                      semantic_monitor->self_reference,
                                      pid,
                                      Atoms.undefined,
                                      semantic_monitor->down_data);

    enif_send(env,
              &semantic_monitor->monitorer_pid,
              semantic_monitor->env,
              down_msg);

    enif_free_env(semantic_monitor->env);
    semantic_monitor->env = NULL;
}

static void delete_semantic_monitor_resource(ErlNifEnv* env, void* resource) {
    UNUSED(env);
    SemanticMonitor* semantic_monitor = (SemanticMonitor*) resource;
    assert(semantic_monitor->env == NULL);
}

/* ------------------------------------------------------------------
 * Helper Definitions
 * ------------------------------------------------------------------ */

static void init_atoms(ErlNifEnv* env) {
    Atoms._DOWN = enif_make_atom(env, "DOWN");
    Atoms._false = enif_make_atom(env, "false");
    Atoms._true = enif_make_atom(env, "true");
    Atoms.badarg = enif_make_atom(env, "badarg");
    Atoms.noproc = enif_make_atom(env, "noproc");
    Atoms.notsup = enif_make_atom(env, "notsup");
    Atoms.process = enif_make_atom(env, "process");
    Atoms.undefined = enif_make_atom(env, "undefined");
}

static ERL_NIF_TERM make_DOWN(ErlNifEnv* env, ERL_NIF_TERM monitor_term,
                              ErlNifPid* pid, ERL_NIF_TERM reason,
                              ERL_NIF_TERM down_data)
{
    return enif_make_tuple6(env,
                            Atoms._DOWN,
                            monitor_term,
                            Atoms.process,
                            enif_make_pid(env, pid),
                            reason,
                            down_data);
}

static ERL_NIF_TERM raise_badarg(ErlNifEnv* env, ERL_NIF_TERM term) {
    // like `enif_raise_badarg/1` but it specifies the actual bad argument
    ERL_NIF_TERM exception_reason = enif_make_tuple2(env, Atoms.badarg, term);
    return enif_raise_exception(env, exception_reason);
}

static ERL_NIF_TERM raise_notsup(ErlNifEnv* env) {
    return enif_raise_exception(env, Atoms.notsup);
}
