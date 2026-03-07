#include "mupdf/fitz.h"
#include "mupdf/pdf.h"

static void pdf_overlay_page(fz_context *ctx, pdf_document *doc_base, pdf_page *page_base, pdf_document *doc_text, pdf_page *page_text, pdf_graft_map *graft_map)
{
	pdf_obj *res_base;
	pdf_obj *res_text;
	pdf_obj *res_base_fonts;
	pdf_obj *res_text_fonts;
	pdf_obj *font_graft;
	pdf_obj *res_text_gstate;
	pdf_obj *res_base_gstate;
	pdf_obj *contents_base;
	pdf_obj *contents_text;
	pdf_obj *contents_text_graft;
	pdf_obj *new_contents = NULL;
	pdf_obj *obj_q1 = NULL;
	pdf_obj *obj_q2 = NULL;
	pdf_obj *obj_Q = NULL;
	fz_buffer *buf = NULL;
	fz_buffer *buf2 = NULL;
	fz_buffer *buf3 = NULL;
	pdf_obj *key, *font;
	fz_matrix mat;
	fz_matrix transmat1;
	fz_matrix transmat2;
	fz_matrix rotmat;
	fz_rect cropbox_base;
	fz_rect cropbox_text;
	int i;
	int n;
	float scale_x;

	fz_var(buf);
	fz_var(buf2);
	fz_var(buf3);
	fz_var(obj_q1);
	fz_var(obj_q2);
	fz_var(obj_Q);
	fz_var(new_contents);
	fz_var(page_base);
	fz_var(page_text);

	res_base = pdf_dict_get(ctx, page_base->obj, PDF_NAME(Resources));
	if (!res_base)
		res_base = pdf_dict_put_dict(ctx, page_base->obj, PDF_NAME(Resources), 4);

	fz_try(ctx)
	{
		cropbox_base = fz_bound_page(ctx, (fz_page *)page_base);
		cropbox_text = fz_bound_page(ctx, (fz_page *)page_text);

		// These are calculated here because the `fz_bound_page` functions do not appear to calculate the same values (unclear why).
		// Do not replace this with the `fz_bound_page` functions without further investigation.
		fz_matrix page_ctm;
		fz_rect rect;
		pdf_page_transform_box(ctx, page_base, &rect, &page_ctm, FZ_CROP_BOX);
		fz_transform_rect(rect, page_ctm);

		int rotate = pdf_dict_get_inheritable_int(ctx, page_base->obj, PDF_NAME(Rotate));

		// The overlay pages we use are always assumed to have 0 rotation at the page level.
		if (rotate == 90 || rotate == 270)
		{
			transmat1 = fz_translate(-(rect.y1 - rect.y0) / 2, -(rect.x1 - rect.x0) / 2);
			transmat2 = fz_translate((rect.x1 - rect.x0) / 2, (rect.y1 - rect.y0) / 2);
		}
		else
		{
			transmat1 = fz_translate(-(rect.x1 - rect.x0) / 2, -(rect.y1 - rect.y0) / 2);
			transmat2 = fz_translate((rect.x1 - rect.x0) / 2, (rect.y1 - rect.y0) / 2);
		}

		float scale_x = cropbox_base.x1 / cropbox_text.x1;

		mat = fz_scale(scale_x, scale_x);
		mat = fz_concat(mat, fz_translate(rect.x0, rect.y0));
		
		rotmat = fz_rotate(rotate);
		mat = fz_concat(mat, transmat1);
		mat = fz_concat(mat, rotmat);
		mat = fz_concat(mat, transmat2);

		res_text = pdf_dict_get(ctx, page_text->obj, PDF_NAME(Resources));
		if (!res_text)
			res_text = pdf_dict_put_dict(ctx, page_text->obj, PDF_NAME(Resources), 4);

		// Ensure that the graphics state is balanced.
		contents_base = pdf_dict_get(ctx, page_base->obj, PDF_NAME(Contents));
		contents_text = pdf_dict_get(ctx, page_text->obj, PDF_NAME(Contents));

		buf = fz_new_buffer(ctx, 1024);
		fz_append_string(ctx, buf, "q\n");
		obj_q1 = pdf_add_stream(ctx, doc_base, buf, NULL, 0);
		fz_drop_buffer(ctx, buf);
		buf = NULL;

		buf2 = fz_new_buffer(ctx, 1024);
		fz_append_string(ctx, buf2, "Q\n");
		obj_Q = pdf_add_stream(ctx, doc_base, buf2, NULL, 0);
		fz_drop_buffer(ctx, buf2);
		buf2 = NULL;

		buf3 = fz_new_buffer(ctx, 1024);
		fz_append_printf(ctx, buf3, "q %g %g %g %g %g %g cm\n", mat.a, mat.b, mat.c, mat.d, mat.e, mat.f);

		obj_q2 = pdf_add_stream(ctx, doc_base, buf3, NULL, 0);
		fz_drop_buffer(ctx, buf3);
		buf3 = NULL;

		contents_text_graft = pdf_graft_object(ctx, doc_base, contents_text);

		if (!pdf_is_array(ctx, contents_base))
		{
			new_contents = pdf_new_array(ctx, doc_base, 10);
			pdf_array_push(ctx, new_contents, obj_q1);
			pdf_array_push(ctx, new_contents, contents_base);
			pdf_array_push(ctx, new_contents, obj_Q);
			pdf_dict_put(ctx, page_base->obj, PDF_NAME(Contents), new_contents);
			pdf_drop_obj(ctx, new_contents);
			contents_base = new_contents;
			new_contents = NULL;
		}

		pdf_array_push(ctx, contents_base, obj_q2);
		pdf_array_push_drop(ctx, contents_base, contents_text_graft);
		pdf_array_push(ctx, contents_base, obj_Q);

		res_base_fonts = pdf_dict_get(ctx, res_base, PDF_NAME(Font));
		res_text_fonts = pdf_dict_get(ctx, res_text, PDF_NAME(Font));
		if (res_text_fonts) {

			if (!res_base_fonts)
			{
				res_base_fonts =  pdf_dict_put_dict(ctx, res_base, PDF_NAME(Font), 1);
			}

			n = pdf_dict_len(ctx, res_text_fonts);
			for (i = 0; i < n; i++)
			{
				font = pdf_dict_get_val(ctx, res_text_fonts, i);
				key = pdf_dict_get_key(ctx, res_text_fonts, i);
				font_graft = pdf_graft_mapped_object(ctx, graft_map, font);
				pdf_dict_put_drop(ctx, res_base_fonts, key, font_graft);
			}
		}

		
		res_base_gstate = pdf_dict_get(ctx, res_base, PDF_NAME(ExtGState));
		res_text_gstate = pdf_dict_get(ctx, res_text, PDF_NAME(ExtGState));
		if (res_text_gstate) {

			if (!res_base_gstate)
			{
				res_base_gstate =  pdf_dict_put_dict(ctx, res_base, PDF_NAME(ExtGState), 1);
			}

			n = pdf_dict_len(ctx, res_text_gstate);
			for (i = 0; i < n; i++)
			{
				font = pdf_dict_get_val(ctx, res_text_gstate, i);
				key = pdf_dict_get_key(ctx, res_text_gstate, i);
				font_graft = pdf_graft_mapped_object(ctx, graft_map, font);
				pdf_dict_put_drop(ctx, res_base_gstate, key, font_graft);
			}
		}

		// Copy annotations from text page to base page
		{
			pdf_obj *annots_text = pdf_dict_get(ctx, page_text->obj, PDF_NAME(Annots));
			int annot_count = pdf_array_len(ctx, annots_text);
			if (annot_count > 0)
			{
				pdf_obj *annots_base = pdf_dict_get(ctx, page_base->obj, PDF_NAME(Annots));
				if (!annots_base)
					annots_base = pdf_dict_put_array(ctx, page_base->obj, PDF_NAME(Annots), annot_count);

				for (int ai = 0; ai < annot_count; ai++)
				{
					pdf_obj *annot_obj = pdf_array_get(ctx, annots_text, ai);
					pdf_obj *annot_graft = pdf_graft_mapped_object(ctx, graft_map, annot_obj);

					fz_rect annot_rect = pdf_dict_get_rect(ctx, annot_graft, PDF_NAME(Rect));
					annot_rect = fz_transform_rect(annot_rect, mat);
					pdf_dict_put_rect(ctx, annot_graft, PDF_NAME(Rect), annot_rect);

					pdf_obj *qp_old = pdf_dict_get(ctx, annot_graft, PDF_NAME(QuadPoints));
					int qp_len = pdf_array_len(ctx, qp_old);
					if (qp_len > 0)
					{
						pdf_obj *qp_new = pdf_new_array(ctx, doc_base, qp_len);
						for (int qi = 0; qi < qp_len; qi += 8)
						{
							fz_quad q = pdf_to_quad(ctx, qp_old, qi);
							q = fz_transform_quad(q, mat);
							pdf_array_push_real(ctx, qp_new, q.ul.x);
							pdf_array_push_real(ctx, qp_new, q.ul.y);
							pdf_array_push_real(ctx, qp_new, q.ur.x);
							pdf_array_push_real(ctx, qp_new, q.ur.y);
							pdf_array_push_real(ctx, qp_new, q.ll.x);
							pdf_array_push_real(ctx, qp_new, q.ll.y);
							pdf_array_push_real(ctx, qp_new, q.lr.x);
							pdf_array_push_real(ctx, qp_new, q.lr.y);
						}
						pdf_dict_put_drop(ctx, annot_graft, PDF_NAME(QuadPoints), qp_new);
					}

					// Remove appearance stream so viewer regenerates it
					pdf_dict_del(ctx, annot_graft, PDF_NAME(AP));

					pdf_array_push_drop(ctx, annots_base, annot_graft);
				}
			}
		}
	}
	fz_always(ctx)
	{
		fz_drop_buffer(ctx, buf);
		pdf_drop_obj(ctx, obj_q1);
		pdf_drop_obj(ctx, obj_q2);
		pdf_drop_obj(ctx, obj_Q);
		pdf_drop_obj(ctx, new_contents);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}
}


void pdf_overlay_documents(fz_context *ctx, pdf_document *doc_base, pdf_document *doc_text)
{
	pdf_page *page_base = NULL;
	pdf_page *page_text = NULL;
	pdf_graft_map *graft_map;
	int i, n;

	fz_var(page_base);
	fz_var(page_text);

	pdf_begin_operation(ctx, doc_base, "Bake interactive content");
	fz_try(ctx)
	{

		graft_map = pdf_new_graft_map(ctx, doc_base);

		n = pdf_count_pages(ctx, doc_base);
		for (i = 0; i < n; ++i)
		{
			page_base = pdf_load_page(ctx, doc_base, i);
			page_text = pdf_load_page(ctx, doc_text, i);

			pdf_overlay_page(ctx, doc_base, page_base, doc_text, page_text, graft_map);

		}

		pdf_end_operation(ctx, doc_base);
	}
	fz_always(ctx)
	{
		pdf_drop_graft_map(ctx, graft_map);
		fz_drop_page(ctx, (fz_page*)page_base);
		fz_drop_page(ctx, (fz_page*)page_text);
	}
	fz_catch(ctx)
	{
		pdf_abandon_operation(ctx, doc_base);
	}
}
