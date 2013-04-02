#include <assert.h>

#include "adios_bp_v1.h"
#include "adios_internals.h"
#include "public/adios_error.h"
#include "public/adios_types.h"
#include "util.h"

#include "adios_selection_util.h"

#include "transforms/adios_transforms_reqgroup.h"
#include "transforms/adios_transforms_common.h"
#include "transforms/adios_transforms_datablock.h"
#include "transforms/adios_transforms_hooks_read.h"
#include "transforms/adios_transforms_read.h"
#include "transforms/adios_transforms_util.h"

#define MYFREE(p) {free(p); (p)=NULL;}

enum ADIOS_TRANSFORM_REQGROUP_RESULT_MODE adios_transform_read_request_get_mode(adios_transform_read_request *req) {
    return req->orig_data != NULL ? FULL_RESULT_MODE : PARTIAL_RESULT_MODE;
}

// Delegate functions

// Returns true for big endian, false for little endian
static int get_system_endianness() {
    uint16_t word = 0x1234;
    return *(uint8_t*)(&word) == 0x12; // Returns 1 (big endian) iff the high byte comes first
}

/*
 * Determines the block indices corresponding to a start and end timestep.
 * Both the input start/end timesteps and the output start/end blockidx are lower bound inclusive, upper bound exclusive: [start, end)
 */
static void compute_blockidx_range(const ADIOS_VARINFO *raw_varinfo, int from_steps, int to_steps, int *start_blockidx, int *end_blockidx) {
    int blockidx;

    // Find the block index for the start and end timestep
    int curblocks = 0;
    for (blockidx = 0; blockidx < raw_varinfo->nsteps; blockidx++) {
        // Find the start block
        if (blockidx == from_steps) {
            *start_blockidx = curblocks;
        }
        curblocks += raw_varinfo->nblocks[blockidx];
        // Find the end block, then stop
        if (blockidx == to_steps - 1) {
            *end_blockidx = curblocks;
            break;
        }
    }
}

static ADIOS_SELECTION * create_pg_bounds(int ndim, ADIOS_VARBLOCK *orig_vb) {
    const uint64_t *new_start = (uint64_t*)bufdup(orig_vb->start, sizeof(uint64_t), ndim);
    const uint64_t *new_count = (uint64_t*)bufdup(orig_vb->count, sizeof(uint64_t), ndim);

    return common_read_selection_boundingbox(ndim, new_start, new_count);
}

adios_transform_read_request * adios_transform_generate_read_reqgroup(const ADIOS_VARINFO *raw_varinfo, const ADIOS_TRANSINFO* transinfo, const ADIOS_FILE *fp,
                                                                       const ADIOS_SELECTION *sel, int from_steps, int nsteps, void *data) {
    // Declares
    adios_transform_read_request *new_reqgroup;
    int blockidx, timestep, timestep_blockidx;
    int curblocks, start_blockidx, end_blockidx;
    int intersects;
    ADIOS_VARBLOCK *raw_vb, *orig_vb;

    enum ADIOS_FLAG swap_endianness = (fp->endianness == get_system_endianness()) ? adios_flag_no : adios_flag_yes;
    int to_steps = from_steps + nsteps;

    // Precondition checking
    assert(is_transform_type_valid(transinfo->transform_type));
    assert(from_steps >= 0 && to_steps <= raw_varinfo->nsteps);

    if (sel->type != ADIOS_SELECTION_BOUNDINGBOX &&
        sel->type != ADIOS_SELECTION_POINTS) {
        adios_error(err_operation_not_supported, "Only bounding box and point selections are currently supported during read on transformed variables.");
        assert(0);
    }

    // Compute the blockidx range, given the timesteps
    compute_blockidx_range(raw_varinfo, from_steps, to_steps, &start_blockidx, &end_blockidx);

    // Retrieve blockinfos, if they haven't been done retrieved
    if (!raw_varinfo->blockinfo)
        common_read_inq_var_blockinfo_raw(fp, raw_varinfo);
    if (!transinfo->orig_blockinfo)
        common_read_inq_trans_blockinfo(fp, raw_varinfo, transinfo);

    // Allocate a new, empty request group
    new_reqgroup = adios_transform_read_request_new(fp, raw_varinfo, transinfo, sel, from_steps, nsteps, data, swap_endianness);

    // Assemble read requests for each varblock
    blockidx = start_blockidx;
    timestep = from_steps;
    timestep_blockidx = 0;
    while (blockidx != end_blockidx) { //for (blockidx = startblock_idx; blockidx != endblock_idx; blockidx++) {
        ADIOS_SELECTION *pg_bounds_sel, *pg_intersection_sel;

        raw_vb = &raw_varinfo->blockinfo[blockidx];
        orig_vb = &transinfo->orig_blockinfo[blockidx];

        pg_bounds_sel = create_pg_bounds(transinfo->orig_ndim, orig_vb);

        // Find the intersection, if any
        pg_intersection_sel = adios_selection_intersect(pg_bounds_sel, sel);

        if (pg_intersection_sel) {
            // Make a PG read request group, and fill it with some subrequests, and link it into the read reqgroup
            adios_transform_pg_read_request *new_pg_reqgroup;

            // Transfer ownership of pg_intersection_to_global_copyspec
            new_pg_reqgroup = adios_transform_pg_read_request_new(timestep, timestep_blockidx,
                                                              blockidx,
                                                              orig_vb, raw_vb,
                                                              pg_intersection_sel,
                                                              pg_bounds_sel);

            adios_transform_generate_read_subrequests(new_reqgroup, new_pg_reqgroup);

            adios_transform_pg_read_request_append(new_reqgroup, new_pg_reqgroup);
        } else {
            // Cleanup
            common_read_selection_delete(pg_bounds_sel);
        }

        // Increment block indexes
        blockidx++;
        timestep_blockidx++;
        if (timestep_blockidx == raw_varinfo->nblocks[timestep]) {
            timestep_blockidx = 0;
            timestep++;
        }
    }

    // If this read request does not intersect any PGs, then clear the new read request
    if (new_reqgroup->num_pg_reqgroups == 0) {
        adios_transform_read_request_free(&new_reqgroup);
        new_reqgroup = NULL;
    }

    return new_reqgroup;
}

/*
 * Called whenever a subreq has been served by the read layer. Marks
 * all subreqs, pg_reqgroups and read_reqgroups as completed as necessary,
 * calls the appropriate hooks in the transform method, and returns an
 * adios_datablock if the transform method produces one.
 */
static adios_datablock * finish_subreq(
        adios_transform_read_request *reqgroup,
        adios_transform_pg_read_request *pg_reqgroup,
        adios_transform_raw_read_request *subreq) {

    adios_datablock *result, *tmp_result;

    // Mark the subrequest as complete
    assert(!subreq->completed && !pg_reqgroup->completed && !reqgroup->completed);
    adios_transform_raw_read_request_mark_complete(reqgroup, pg_reqgroup, subreq);

    // Invoke all callbacks, depending on what completed, and
    // get at most one ADIOS_VARCHUNK to return
    result = adios_transform_subrequest_completed(reqgroup, pg_reqgroup, subreq);

    if (pg_reqgroup->completed) {
        tmp_result = adios_transform_pg_reqgroup_completed(reqgroup, pg_reqgroup);
        if (tmp_result) {
            assert(!result); // pg_reqgroup_completed returned a result, but subrequest_completed did as well
            result = tmp_result;
        }
    }

    if (reqgroup->completed) {
        tmp_result = adios_transform_read_reqgroup_completed(reqgroup);
        if (tmp_result) {
            assert(!result); // read_reqgroup_completed returned a result, but subrequest_completed or pg_reqgroup_completed did as well
            result = tmp_result;
        }
    }

    return result;
}

/*
 * Takes a datablock and applies its data to the user buffer for the given
 * read request group, then frees the given datablock. Assumes there is, in
 * fact, a user buffer (i.e., it is not NULL).
 *
 * Assumes that the datablock selection is of type bounding box.
 *
 * NOTE: also frees the data buffer within the datablock
 *
 * @return non-zero if some data in the datablock intersected the read
 *         request's selection, and was applied; returns 0 otherwise.
 */
static int apply_datablock_to_result_and_free(adios_datablock *datablock,
                                              adios_transform_read_request *reqgroup) {
    adios_subvolume_copy_spec *copyspec = malloc(sizeof(adios_subvolume_copy_spec));

    assert(datablock); assert(reqgroup);
    assert(reqgroup->orig_sel);
    assert(reqgroup->orig_data);

    if (datablock->bounds->type != ADIOS_SELECTION_BOUNDINGBOX) {
        adios_error(err_operation_not_supported,
                    "Only results of bounding box selection type are currently accepted "
                    "from transform plugins (received selection type %d)",
                    datablock->bounds->type);
        assert(0);
    }

    uint64_t used_count =
            adios_patch_data(reqgroup->orig_data, reqgroup->orig_sel,
                             datablock->data, datablock->bounds,
                             datablock->elem_type, reqgroup->swap_endianness);

    adios_datablock_free(&datablock, 1);
    //return intersects;
    return used_count != 0;
}

static uint64_t compute_selection_size_in_bytes(const ADIOS_SELECTION *sel,
                                                enum ADIOS_DATATYPES datum_type,
                                                const ADIOS_TRANSINFO *transinfo) {
    int typesize = adios_get_type_size(datum_type, NULL);
    switch (sel->type) {
    case ADIOS_SELECTION_BOUNDINGBOX:
    {
        const ADIOS_SELECTION_BOUNDINGBOX_STRUCT *bb = &sel->u.bb;
        const int ndim = bb->ndim;
        int i;

        uint64_t size = typesize;
        for (i = 0; i < ndim; i++)
            size *= bb->count[i];

        return size;
    }
    case ADIOS_SELECTION_POINTS:
    {
        const ADIOS_SELECTION_POINTS_STRUCT *pts = &sel->u.points;
        return pts->ndim * pts->npoints * typesize;
    }
    case ADIOS_SELECTION_WRITEBLOCK:
    {
        const ADIOS_SELECTION_WRITEBLOCK_STRUCT *wb = &sel->u.block;
        const ADIOS_VARBLOCK *theblock = &transinfo->orig_blockinfo[wb->index];
        int i;

        uint64_t size = typesize;
        for (i = 0; i < transinfo->orig_ndim; i++)
            size *= theblock->count[i];

        return size;
    }
    case ADIOS_SELECTION_AUTO:
    default:
        adios_error_at_line(err_invalid_argument, __FILE__, __LINE__, "Unsupported selection type %d", sel->type);
        return 0;
    }
}

/*
 * Takes a datablock containing data potentially applicable to the given read
 * request group, identifies that data (if any), and returns it as an
 * ADIOS_VARCHUNK. Additionally, free the datablock.
 *
 * NOTE: This function transfers ownership of the ->data field of the datablock
 * and places it in the returned ADIOS_VARCHUNK (if an ADIOS_VARCHUNK is
 * returned).
 */
static ADIOS_VARCHUNK * apply_datablock_to_chunk_and_free(adios_datablock *result, adios_transform_read_request *reqgroup) {
    ADIOS_VARCHUNK *chunk;
    uint64_t *inter_goffset;
    uint64_t *inter_offset_within_result;
    uint64_t *inter_dims;

    uint64_t chunk_buffer_size;
    ADIOS_SELECTION *inter_sel;

    assert(result); assert(reqgroup);
    assert(reqgroup->orig_sel);

    inter_sel = adios_selection_intersect(result->bounds, reqgroup->orig_sel);

    if (inter_sel) {
        // TODO: This data copy code is somewhat inefficient, as it requires a second buffer,
        //       whereas it may be possible to "compact" the buffer in-place, removing only
        //       those values that are outside the selection. This would require another large
        //       chunk of selection-type-pairwise-specific code, as in adios_patchdata.c and
        //       adios_selection_util.c, so we use this approach to avoid that here. If it
        //       ends up being slow, this can be fixed.

        // Compute the number of bytes to allocate for this chunk
        chunk_buffer_size = compute_selection_size_in_bytes(inter_sel, result->elem_type, reqgroup->transinfo);

        chunk = malloc(sizeof(ADIOS_VARCHUNK));
        chunk->data = malloc(chunk_buffer_size);
        chunk->sel = inter_sel;

        adios_patch_data(chunk->data, chunk->sel,
                         result->data, result->bounds,
                         result->elem_type, reqgroup->swap_endianness);

        // Populate the chunk struct
        chunk->varid = reqgroup->raw_varinfo->varid;
        chunk->type = result->elem_type;

        common_read_selection_delete(inter_sel);
    }

    adios_datablock_free(&result, 1); // 1 == free the datablock's buffer, as well
    return chunk;
}

static ADIOS_VARCHUNK * extract_chunk_from_finished_read_reqgroup(adios_transform_read_request *reqgroup) {
    assert(reqgroup);
    assert(reqgroup->completed);

    ADIOS_VARCHUNK *chunk = malloc(sizeof(ADIOS_VARCHUNK));
    chunk->varid = reqgroup->raw_varinfo->varid;
    chunk->type = reqgroup->transinfo->orig_type;

    // Transfer ownership of orig_data
    chunk->data = reqgroup->orig_data;
    reqgroup->orig_data = NULL;

    // Transfer ownership of orig_sel
    chunk->sel = (ADIOS_SELECTION*)reqgroup->orig_sel; // Remove const
    reqgroup->orig_sel = NULL;

    return chunk;
}

// Take an ADIOS_VARCHUNK that was just read and process it with the transform
// system. If it was part of a read request corresponding to a transformed
// variable, consume it, and possibly replacing it with a detransformed chunk.
// Otherwise, do nothing.
void adios_transform_process_read_chunk(adios_transform_read_request **reqgroups_head, ADIOS_VARCHUNK ** chunk) {
    adios_transform_read_request *reqgroup;
    adios_transform_pg_read_request *pg_reqgroup;
    adios_transform_raw_read_request *subreq;
    adios_datablock *result, *tmp_result;

    // Find the subrequest that matches the VARCHUNK that was just read (if any)
    int found = adios_transform_read_request_list_match_chunk(*reqgroups_head, *chunk, 1, &reqgroup, &pg_reqgroup, &subreq);

    // If no subrequest matches the VARCHUNK, it must correspond to a non-transformed variable.
    // In this case, return immediately and let it be processed as-is.
    if (!found)
        return;

    // Otherwise, this VARCHUNK corresponds to a subrequest.
    // Therefore, consume it, and perhaps replace it with a detransformed chunk.

    // Consume the chunk, as it will be passed to a transform method and should
    // not be processed by the caller.
    // (NOTE: Freeing this does not free the memory it points to)
    common_read_free_chunk(*chunk);
    *chunk = NULL;

    // Next, free any buffers held by the last-returned VARCHUNK, as they are now invalidated
    // by the user's call to check_reads (which in turn is the caller of this function)
    if (reqgroup->lent_varchunk && reqgroup->lent_varchunk->data)
        free(reqgroup->lent_varchunk->data);

    // Next, update the subreq that corresponds to this VARCHUNK as completed, retrieving any
    // produced result
    result = finish_subreq(reqgroup, pg_reqgroup, subreq);

    // Now, if a new adios_datablock is now available as a result of the above completed subreq,
    // apply it as a result for the user in a way appropriate to the current result mode
    if (result) {
        // Then, return data as appropriate depending on the return mode of this read operation
        //   PARTIAL: no user-allocated buffer is given for the full result, so results must be
        //            returned one VARCHUNK at a time.
        //   FULL: the user has supplied a buffer for full results, so patch relevant data from
        //         the returned VARCHUNK into this buffer.
        enum ADIOS_TRANSFORM_REQGROUP_RESULT_MODE result_mode = adios_transform_read_request_get_mode(reqgroup);
        switch (result_mode) {
        case PARTIAL_RESULT_MODE:
            // Apply this VARCHUNK
            *chunk = apply_datablock_to_chunk_and_free(result, reqgroup);

            reqgroup->lent_varchunk = *chunk;
            break;
        case FULL_RESULT_MODE:
            apply_datablock_to_result_and_free(result, reqgroup);

            // If the whole variable is now ready, return it as a VARCHUNK
            // Otherwise, return no chunk (NULL)
            if (reqgroup->completed) {
                *chunk = extract_chunk_from_finished_read_reqgroup(reqgroup);
            } else {
                assert(!*chunk); // No chunk to return, and *chunk is already NULL
            }
            break;
        }
    } else {
        assert(!*chunk); // No chunk to return, and *chunk is already NULL
    }

    // Free the read request group if it was completed
    if (reqgroup->completed) {
        adios_transform_read_request_remove(reqgroups_head, reqgroup);
        adios_transform_read_request_free(&reqgroup);
    }
}

/*
 * Process all read reqgroups, assuming they have been fully completed,
 * producing all required results based on the raw data read.
 * (This function is called after a blocking perform_reads completes)
 */
void adios_transform_process_all_reads(adios_transform_read_request **reqgroups_head) {
    // Mark all subrequests, PG request groups and read request groups
    // as completed, calling callbacks as needed
    adios_transform_read_request *reqgroup;
    adios_transform_pg_read_request *pg_reqgroup;
    adios_transform_raw_read_request *subreq;
    adios_datablock *result;

    // Complete each read reqgroup in turn
    while ((reqgroup = adios_transform_read_request_pop(reqgroups_head)) != NULL) {
        // Free leftover read request groups immediately, with no further processing
        if (reqgroup->completed) {
            adios_transform_read_request_free(&reqgroup);
            continue;
        }

        // Complete every child PG reqgroup
        for (pg_reqgroup = reqgroup->pg_reqgroups; pg_reqgroup; pg_reqgroup = pg_reqgroup->next) {
            // Skip completed PG reqgroups
            if (pg_reqgroup->completed) continue;

            // Complete every child subreq
            for (subreq = pg_reqgroup->subreqs; subreq; subreq = subreq->next) {
                // Skip completed subreqs
                if (subreq->completed) continue;

                // Mark the subreq as completed
                adios_transform_raw_read_request_mark_complete(reqgroup, pg_reqgroup, subreq);
                assert(subreq->completed);

                // Make the required call to the transform method to apply the results
                result = adios_transform_subrequest_completed(reqgroup, pg_reqgroup, subreq);
                if (result) apply_datablock_to_result_and_free(result, reqgroup);
            }
            assert(pg_reqgroup->completed);

            // Make the required call to the transform method to apply the results
            result = adios_transform_pg_reqgroup_completed(reqgroup, pg_reqgroup);
            if (result) apply_datablock_to_result_and_free(result, reqgroup);
        }
        assert(reqgroup->completed);

        // Make the required call to the transform method to apply the results
        result = adios_transform_read_reqgroup_completed(reqgroup);
        if (result) apply_datablock_to_result_and_free(result, reqgroup);

        // Now that the read reqgroup has been processed, free it (which also frees all children)
        adios_transform_read_request_free(&reqgroup);
    }
}
