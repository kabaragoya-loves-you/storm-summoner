/**
 * @file lv_draw_ppa_fill.c
 *
 */

#include "lv_draw_ppa_private.h"
#include "lv_draw_ppa.h"

#if LV_USE_PPA

void lv_draw_ppa_fill(lv_draw_task_t * t, const lv_draw_fill_dsc_t * dsc,
                      const lv_area_t * coords)
{
    lv_draw_ppa_unit_t * u = (lv_draw_ppa_unit_t *)t->draw_unit;
    lv_draw_buf_t * draw_buf = t->target_layer->draw_buf;
    int width  = lv_area_get_width(coords);
    int height = lv_area_get_height(coords);

    if(width <= 0 || height <= 0) {
        LV_LOG_WARN("Invalid draw area for filling!");
        return;
    }

    int pic_w = draw_buf->header.stride / lv_color_format_get_size(draw_buf->header.cf);

    ppa_fill_oper_config_t cfg = {
        .fill_argb_color.val = lv_color_to_u32(dsc->color),
        .fill_block_w    = width,
        .fill_block_h    = height,
        .out = {
            .buffer         = draw_buf->data,
            .buffer_size    = draw_buf->data_size,
            // PPA calculates stride as pic_w * bytes_per_pixel.
            // We must use the buffer's actual stride to ensure correct addressing.
            .pic_w          = pic_w,
            .pic_h          = draw_buf->header.h,  // Full buffer height
            .block_offset_x = coords->x1 - t->target_layer->buf_area.x1,
            .block_offset_y = coords->y1 - t->target_layer->buf_area.y1,
            .fill_cm        = lv_color_format_to_ppa_fill(draw_buf->header.cf),
        },

        .mode            = PPA_TRANS_MODE_BLOCKING,
        .user_data       = u,
    };

    esp_err_t ret = ppa_do_fill(u->fill_client, &cfg);
    if(ret != ESP_OK) {
        LV_LOG_ERROR("PPA fill failed: %d", ret);
    }
    
    // In blocking mode, PPA is done when function returns.
    // Sync cache immediately so CPU sees the result.
    esp_cache_msync(draw_buf->data, draw_buf->data_size, ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
}

#endif /* LV_USE_PPA */
