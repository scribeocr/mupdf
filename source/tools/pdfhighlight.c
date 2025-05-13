// Copyright (C) 2004-2024 Artifex Software, Inc.
//
// This file is part of MuPDF.
//
// MuPDF is free software: you can redistribute it and/or modify it under the
// terms of the GNU Affero General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// MuPDF is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
// details.
//
// You should have received a copy of the GNU Affero General Public License
// along with MuPDF. If not, see <https://www.gnu.org/licenses/agpl-3.0.en.html>
//
// Alternative licensing terms are available from the licensor.
// For commercial licensing, see <https://www.artifex.com/> or contact
// Artifex Software, Inc., 39 Mesa Street, Suite 108A, San Francisco,
// CA 94129, USA, for further information.

#include "mupdf/fitz.h"
#include "mupdf/pdf.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static int usage(void)
{
    fprintf(stderr,
        "usage: mutool highlight [options] input.pdf [output.pdf] regions\n"
        "\t-p -\tpassword\n"
        "\n"
        "\tregions\tlist of regions to highlight in format: page,x0,y0,x1,y1[;page,x0,y0,x1,y1...]\n"
        "\t\tpage: page number (1-based)\n"
        "\t\tx0,y0: coordinates of top-left corner of highlight rectangle\n"
        "\t\tx1,y1: coordinates of bottom-right corner of highlight rectangle\n"
        "\t\tExample: \"1,50,50,100,100\" - highlights one region on page 1\n"
        "\t\tExample: \"1,50,50,100,100;2,60,60,120,120\" - highlights regions on pages 1 and 2\n"
    );
    return 1;
}

typedef struct {
    int page;
    int x0, y0, x1, y1;
} HighlightRegion;

// Parse a string of regions in the format: page,x0,y0,x1,y1;page,x0,y0,x1,y1;...
// Returns the number of regions parsed
static int parse_regions(const char *regions_str, HighlightRegion **regions_out)
{
    int count = 0;
    int capacity = 10; // Initial capacity
    HighlightRegion *regions = malloc(capacity * sizeof(HighlightRegion));
    if (!regions) return 0;
    
    const char *p = regions_str;
    
    while (*p)
    {
        // Allocate more space if needed
        if (count >= capacity)
        {
            capacity *= 2;
            HighlightRegion *new_regions = realloc(regions, capacity * sizeof(HighlightRegion));
            if (!new_regions)
            {
                free(regions);
                return 0;
            }
            regions = new_regions;
        }
        
        // Parse the 5 values for this region
        int read = sscanf(p, "%d,%d,%d,%d,%d", 
            &regions[count].page, 
            &regions[count].x0, 
            &regions[count].y0, 
            &regions[count].x1, 
            &regions[count].y1);
            
        if (read != 5)
        {
            fprintf(stderr, "Error parsing region spec at: %s\n", p);
            free(regions);
            return 0;
        }
        
        count++;
        
        // Skip to next region (after semicolon) or end of string
        while (*p && *p != ';')
            p++;
            
        // Skip the semicolon if present
        if (*p == ';')
            p++;
    }
    
    *regions_out = regions;
    return count;
}

int pdfhighlight_main(int argc, char **argv)
{
    char *infile = NULL;
    char *outfile = "out.pdf";
    char *password = "";
    char *regions_str = NULL;
    int c;
    fz_context *ctx;
    pdf_document *doc = NULL;
    int errors = 0;
    HighlightRegion *regions = NULL;
    int region_count = 0;

    while ((c = fz_getopt(argc, argv, "p:")) != -1)
    {
        switch (c)
        {
        case 'p': password = fz_optarg; break;
        default: return usage();
        }
    }

    if (fz_optind >= argc)
        return usage();

    infile = argv[fz_optind++];

    if (fz_optind < argc && (strstr(argv[fz_optind], ".pdf") || strstr(argv[fz_optind], ".PDF")))
        outfile = argv[fz_optind++];

    if (fz_optind >= argc)
        return usage();

    regions_str = argv[fz_optind++];
    region_count = parse_regions(regions_str, &regions);
    
    if (region_count <= 0)
    {
        fprintf(stderr, "No valid regions found\n");
        return 1;
    }

    ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx)
    {
        fprintf(stderr, "cannot initialize context\n");
        free(regions);
        return 1;
    }

    fz_try(ctx)
    {
        doc = pdf_open_document(ctx, infile);
        if (pdf_needs_password(ctx, doc))
        {
            if (!pdf_authenticate_password(ctx, doc, password))
                fz_throw(ctx, FZ_ERROR_GENERIC, "cannot authenticate password: %s", infile);
        }
        
        int num_pages = pdf_count_pages(ctx, doc);
        int regions_highlighted = 0;

        // Process each region
        for (int i = 0; i < region_count; i++) 
        {
            int page = regions[i].page;
            int x0 = regions[i].x0;
            int y0 = regions[i].y0;
            int x1 = regions[i].x1;
            int y1 = regions[i].y1;
            
            // Validate page number
            if (page <= 0 || page > num_pages)
            {
                fprintf(stderr, "Warning: page number out of range: %d (document has %d pages), skipping\n", 
                    page, num_pages);
                continue;
            }

            // Validate coordinates
            if (x0 > x1 || y0 > y1)
            {
                fprintf(stderr, "Warning: invalid highlight rectangle: (%d,%d,%d,%d), skipping\n", 
                    x0, y0, x1, y1);
                continue;
            }

            // Page numbers in the API are 1-based (for users), but 0-based internally
            pdf_highlight_page(doc, ctx, page - 1, x0, y0, x1, y1);
            regions_highlighted++;
        }
        
        // Only save if we highlighted at least one region successfully
        if (regions_highlighted > 0) 
        {
            pdf_save_document(ctx, doc, outfile, NULL);
            fprintf(stderr, "Highlighted %d regions in %s and saved to %s\n", 
                regions_highlighted, infile, outfile);
        }
        else
        {
            fprintf(stderr, "No regions were highlighted. Output file not created.\n");
            errors++;
        }
    }
    fz_catch(ctx)
    {
        fz_report_error(ctx);
        errors++;
    }

    if (regions)
        free(regions);
    
    pdf_drop_document(ctx, doc);
    fz_drop_context(ctx);

    return errors != 0;
}