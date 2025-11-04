# Sparse Buffer Investigation Summary

## Overview
We investigated implementing a sparse buffer system at the LVGL library level to reduce RAM and CPU requirements for circular displays by only storing and processing visible pixels within the circle.

## Goal
- Use the `coordinate_map` to store only visible pixels (~12,929 pixels instead of 16,384)
- Reduce RAM usage by ~21% 
- Potentially improve performance by not processing invisible pixels

## Key Findings

### 1. LVGL Architecture Challenges
- LVGL uses direct pointer arithmetic extensively throughout its codebase
- The draw buffer system expects contiguous memory layout
- Blend operations directly calculate pixel addresses using stride
- No central pixel access function to intercept

### 2. Possible Implementation Approaches

#### Approach A: Custom Draw Buffer Handlers
- LVGL provides handler callbacks for buffer operations
- However, these only handle allocation/free, not pixel-level access
- Would require modifying core LVGL drawing functions

#### Approach B: Layer-Level Interception
- Create custom layer types that compress/decompress on demand
- Problem: Layers are created deep in the refresh cycle
- Would need to modify `lv_refr.c` core refresh logic

#### Approach C: Flush-Level Compression (Mode 4)
- Intercept at flush callback to analyze/compress data
- This is what we implemented as a demonstration
- Shows compression potential but doesn't actually save memory

#### Approach D: True Sparse Buffer in LVGL Core
- Would require modifying:
  - `lv_draw_buf_goto_xy()` to handle coordinate mapping
  - All blend functions to understand sparse addressing
  - Buffer allocation to use compressed size
  - Refresh logic to handle non-contiguous memory

## Implementation: Mode 4 Demonstration

We created Mode 4 as a proof-of-concept that:
1. Hooks the flush callback
2. Analyzes each flush to count visible vs. invisible pixels
3. Reports compression potential
4. Shows ~21% memory savings would be possible

### Key Files Created:
- `sparse_buffer.c` - Implementation showing compression analysis
- `sparse_buffer.h` - Simple API for enabling/disabling analysis

## Conclusions

1. **Feasibility**: A true sparse buffer at LVGL core level would require extensive modifications throughout the library.

2. **Complexity**: The pervasive use of direct pointer arithmetic makes this a non-trivial change.

3. **Alternative**: The existing Mode 2 (coordinate map in flush) provides similar benefits with minimal LVGL changes.

4. **Recommendation**: For production use, either:
   - Use Mode 2 for practical memory savings
   - Contribute sparse buffer support to LVGL upstream
   - Implement compression at a different level (e.g., display controller)

## Performance Comparison

| Mode | Description | Memory Usage | Complexity | LVGL Changes |
|------|-------------|--------------|------------|--------------|
| 0 | Full buffer | 32KB | Low | None |
| 1 | Dynamic culling | 8KB | Medium | None |
| 2 | Coordinate map | 8KB | Medium | None |
| 3 | Callback wrapper | 8KB | Low | None |
| 4 | Sparse (demo) | 8KB* | High | Extensive |

*Mode 4 doesn't actually save memory in current implementation, just demonstrates potential

## Future Work

If pursuing true sparse buffers:
1. Fork LVGL and modify core buffer access functions
2. Create sparse-aware blend operations
3. Implement coordinate translation layer
4. Extensive testing for performance impact

The investigation shows that while technically possible, implementing sparse buffers at the LVGL core level would be a significant undertaking requiring deep modifications to the library's architecture. 