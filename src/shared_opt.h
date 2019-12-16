#ifndef _SHARED_OPT_H_
#define _SHARED_OPT_H_
#include <gtk/gtk.h>
extern gboolean sp_embed_images_sh;
extern char* sp_export_svg_path_sh;
extern gboolean sp_export_svg_sh;
#define FAST_SVG_DEFAULT 99999999999
extern gboolean sp_adjust_mask_size_sh;
extern gboolean sp_map_drop_color_sh;
extern gboolean sp_split_spec_sh;
extern gboolean sp_bullet_point1f_sh;
extern gint64 sp_fast_svg_sh;
extern gint sp_gradient_precision_sh;
extern gboolean sp_log_font_sh;
extern gboolean sp_export_fonts_sh;
extern gboolean sp_cid_to_ttf_sh;
extern gboolean sp_original_fonts_sh;
extern gboolean sp_show_counters_sh;
extern gchar *sp_fonts_dir_sh;
extern gboolean sp_merge_images_sh;
extern gboolean sp_bleed_marks_sh;
extern gint sp_bleed_left_sh;
extern gint sp_bleed_right_sh;
extern gint sp_bleed_top_sh;
extern gint sp_bleed_bottom_sh;
extern gboolean sp_crop_mark_sh;
extern gint sp_crop_mark_shift_sh;
extern gboolean sp_merge_path_sh;
extern gboolean sp_add_background_sh;
extern gboolean sp_use_dx_sh;
extern gboolean sp_create_jpeg_sp;
extern gboolean sp_try_origin_jpeg_sp;
extern gboolean sp_preserve_dpi_sp;
extern gboolean sp_merge_jpeg_sp;
extern gboolean sp_merge_mask_clean_sp;
extern gboolean sp_mapping_off_sh;
extern gboolean sp_merge_mask_sh;
extern char *sp_font_postfix_sh;
extern char *sp_font_default_font_sh;
extern gint sp_merge_limit_sh;
extern gint sp_merge_limit_path_sh;
extern gboolean sp_rect_how_path_sh;
extern gint sp_thumb_width_sh;
extern float sp_export_dpi_sh;
extern gboolean warning1IsNotText;
extern gboolean warning2wasRasterized;
extern gboolean warning3tooManyImages;
#endif

