#include <nm_core.h>
#include <nm_form.h>
#include <nm_menu.h>
#include <nm_utils.h>
#include <nm_string.h>
#include <nm_vector.h>
#include <nm_window.h>
#include <nm_network.h>
#include <nm_database.h>
#include <nm_cfg_file.h>

#define NM_LAN_FIELDS_NUM 2

#define NM_LAN_GET_VETH_SQL \
    "SELECT (l_name || '<->' || r_name) FROM veth"
#define NM_LAN_ADD_VETH_SQL \
    "INSERT INTO veth(l_name, r_name) VALUES ('%s', '%s')"
#define NM_LAN_CHECK_NAME_SQL \
    "SELECT id FROM veth WHERE l_name='%s' OR r_name='%s'"

extern sig_atomic_t redraw_window;

static nm_field_t *fields[NM_LAN_FIELDS_NUM + 1];

enum {
    NM_FLD_LNAME = 0,
    NM_FLD_RNAME
};

static void nm_lan_help(void);
static void nm_lan_add_veth(void);
static void nm_lan_del_veth(const nm_str_t *name);
static int nm_lan_add_get_data(nm_str_t *ln, nm_str_t *rn);

void nm_lan_settings(void)
{
    int ch = 0, regen_data = 1;
    nm_str_t query = NM_INIT_STR;
    nm_window_t *veth_window = NULL;
    nm_vect_t veths = NM_INIT_VECT;
    nm_vect_t veths_list = NM_INIT_VECT;
    nm_menu_data_t veths_data = NM_INIT_MENU_DATA;
    uint32_t list_max = 0;

    nm_str_alloc_text(&query, NM_LAN_GET_VETH_SQL);

    keypad(stdscr, 1);

    nm_db_select(query.data, &veths);

    for (;;)
    {
        nm_print_vm_window();

        if (regen_data)
        {
            nm_vect_free(&veths_list, NULL);
            nm_vect_free(&veths, nm_str_vect_free_cb);
            nm_db_select(query.data, &veths);
        }

        if (veths.n_memb == 0)
        {
            int col;
            size_t msg_len;
            const char *msg = _("No VETH interfaces configured");

            col = getmaxx(stdscr);
            msg_len = mbstowcs(NULL, msg, strlen(msg));
            mvprintw(4, (col - msg_len) / 2, msg);
        }

        if (regen_data)
        {
            veths_data.highlight = 1;
            list_max = nm_cfg_get()->list_max;

            if (list_max > veths.n_memb)
                list_max = veths.n_memb;

            veths_data.item_last = list_max;

            for (size_t n = 0; n < veths.n_memb; n++)
            {
                nm_menu_item_t veth = NM_INIT_MENU_ITEM;
                veth.name = (nm_str_t *) nm_vect_at(&veths, n);
                nm_vect_insert(&veths_list, &veth, sizeof(veth), NULL);
            }

            veths_data.v = &veths_list;

            if (veth_window)
            {
                delwin(veth_window);
                veth_window = NULL;
            }
            veth_window = nm_init_window(list_max + 4, 40, 7);
            nm_print_veth_menu(veth_window, &veths_data, 1);
            regen_data = 0;
        }
        else
        {
            veth_window = nm_init_window(list_max + 4, 40, 7);
            nm_print_veth_menu(veth_window, &veths_data, 0);
        }

        ch = getch();

        if ((veths.n_memb > 0) && (ch == KEY_UP) && (veths_data.highlight == 1) &&
            (veths_data.item_first == 0) && (list_max < veths_data.v->n_memb))
        {
            veths_data.highlight = list_max;
            veths_data.item_first = veths_data.v->n_memb - list_max;
            veths_data.item_last = veths_data.v->n_memb;
        }

        else if ((veths.n_memb > 0) && (ch == KEY_UP))
        {
            if ((veths_data.highlight == 1) && (veths_data.v->n_memb <= list_max))
                 veths_data.highlight = veths_data.v->n_memb;
            else if ((veths_data.highlight == 1) && (veths_data.item_first != 0))
            {
                veths_data.item_first--;
                veths_data.item_last--;
            }
            else
            {
                veths_data.highlight--;
            }
        }

        else if ((veths.n_memb > 0) && (ch == KEY_DOWN) &&
                 (veths_data.highlight == list_max) &&
                 (veths_data.item_last == veths_data.v->n_memb))
        {
            veths_data.highlight = 1;
            veths_data.item_first = 0;
            veths_data.item_last = list_max;
        }

        else if ((veths.n_memb > 0) && (ch == KEY_DOWN))
        {
            if ((veths_data.highlight == veths_data.v->n_memb) &&
                (veths_data.v->n_memb <= list_max))
                 veths_data.highlight = 1;
            else if ((veths_data.highlight == list_max) &&
                     (veths_data.item_last < veths_data.v->n_memb))
            {
                veths_data.item_first++;
                veths_data.item_last++;
            }
            else
            {
                veths_data.highlight++;
            }
        }

        else if (ch == NM_KEY_A)
        {
            nm_lan_add_veth();
            regen_data = 1;
        }

        else if (ch == NM_KEY_R)
        {
            if (veths.n_memb > 0)
                nm_lan_del_veth(nm_vect_item_name_cur(veths_data));
            regen_data = 1;
        }

        else if (ch == KEY_F(1))
            nm_lan_help();

        else if (ch == NM_KEY_ESC)
            goto out;

        if (redraw_window)
        {
            endwin();
            refresh();
            clear();
            redraw_window = 0;
        }
    }

out:
    nm_vect_free(&veths, nm_str_vect_free_cb);
    nm_vect_free(&veths_list, NULL);
    nm_str_free(&query);
    keypad(stdscr, 0);
}

static void nm_lan_help(void)
{
    nm_window_t *w = NULL;

    const char *msg[] = {
          "             nEMU v" NM_VERSION,
          "",
        _(" a - add veth interface"),
        _(" r - remove veth interface"),
        _(" u - up veth interface"),
        _(" d - down veth interface")
    };

    w = nm_init_window(9, 38, 1);
    box(w, 0, 0);

    for (size_t n = 0, y = 1; n < nm_arr_len(msg); n++, y++)
        mvwprintw(w, y, 1, "%s", msg[n]);

    wgetch(w);
    delwin(w);
}

static void nm_lan_add_veth(void)
{
    nm_str_t l_name = NM_INIT_STR;
    nm_str_t r_name = NM_INIT_STR;
    nm_str_t query = NM_INIT_STR;
    nm_form_t *form = NULL;
    nm_window_t *window = NULL;
    nm_spinner_data_t sp_data = NM_INIT_SPINNER;
    size_t msg_len;
    pthread_t spin_th;
    int done = 0;

    nm_print_title(_(NM_EDIT_TITLE));
    window = nm_init_window(9, 45, 3);

    init_pair(1, COLOR_BLACK, COLOR_WHITE);
    wbkgd(window, COLOR_PAIR(1));

    for (size_t n = 0; n < NM_LAN_FIELDS_NUM; ++n)
    {
        fields[n] = new_field(1, 19, (n + 1) * 2, 1, 0, 0);
        set_field_back(fields[n], A_UNDERLINE);
    }

    fields[NM_LAN_FIELDS_NUM] = NULL;

    set_field_type(fields[NM_FLD_LNAME], TYPE_REGEXP, "^[a-zA-Z0-9_-]{1,15} *$");
    set_field_type(fields[NM_FLD_RNAME], TYPE_REGEXP, "^[a-zA-Z0-9_-]{1,15} *$");

    mvwaddstr(window, 1, 2, _("Create VETH interface"));
    mvwaddstr(window, 4, 2, _("Name"));
    mvwaddstr(window, 6, 2, _("Peer name"));

    form = nm_post_form(window, fields, 22);
    if (nm_draw_form(window, form) != NM_OK)
        goto out;

    if (nm_lan_add_get_data(&l_name, &r_name) != NM_OK)
        goto out;

    msg_len = mbstowcs(NULL, _(NM_EDIT_TITLE), strlen(_(NM_EDIT_TITLE)));
    sp_data.stop = &done;
    sp_data.x = (getmaxx(stdscr) + msg_len + 2) / 2;

    if (pthread_create(&spin_th, NULL, nm_spinner, (void *) &sp_data) != 0)
        nm_bug(_("%s: cannot create thread"), __func__);

    nm_net_add_veth(&l_name, &r_name);
    nm_net_link_up(&l_name);
    nm_net_link_up(&r_name);

    nm_str_format(&query, NM_LAN_ADD_VETH_SQL, l_name.data, r_name.data);
    nm_db_edit(query.data);

    done = 1;
    if (pthread_join(spin_th, NULL) != 0)
        nm_bug(_("%s: cannot join thread"), __func__);

out:
    nm_form_free(form, fields);
    nm_str_free(&l_name);
    nm_str_free(&r_name);
    nm_str_free(&query);
}

static int nm_lan_add_get_data(nm_str_t *ln, nm_str_t *rn)
{
    int rc = NM_OK;
    nm_str_t query = NM_INIT_STR;
    nm_vect_t err = NM_INIT_VECT;
    nm_vect_t names = NM_INIT_VECT;

    nm_get_field_buf(fields[NM_FLD_LNAME], ln);
    nm_get_field_buf(fields[NM_FLD_RNAME], rn);

    nm_form_check_datap(_("Name"), ln, err);
    nm_form_check_datap(_("Peer name"), rn, err);

    if ((rc = nm_print_empty_fields(&err)) == NM_ERR)
    {
        nm_vect_free(&err, NULL);
        goto out;
    }

    nm_str_format(&query, NM_LAN_CHECK_NAME_SQL, ln->data, ln->data);
    nm_db_select(query.data, &names);
    if (names.n_memb > 0)
    {
        nm_print_warn(3, 6, _("Name is already taken"));
        rc = NM_ERR;
        goto out;
    }

    nm_str_trunc(&query, 0);
    nm_str_format(&query, NM_LAN_CHECK_NAME_SQL, rn->data, rn->data);
    nm_db_select(query.data, &names);
    if (names.n_memb > 0)
    {
        nm_print_warn(3, 6, _("Peer name is already taken"));
        rc = NM_ERR;
    }

out:
    nm_vect_free(&names, nm_str_vect_free_cb);
    nm_str_free(&query);
    return rc;
}

static void nm_lan_del_veth(const nm_str_t *name)
{
    nm_str_t name_copy = NM_INIT_STR;
    nm_str_t lname = NM_INIT_STR;
    nm_str_t query = NM_INIT_STR;
    char *cp = NULL;

    nm_str_copy(&name_copy, name);
    cp = strchr(name_copy.data, '<');
    if (cp)
    {
        *cp = '\0';
        nm_str_alloc_text(&lname, name_copy.data);
    }

    nm_net_del_iface(&lname);

    nm_str_format(&query, "DELETE FROM veth WHERE l_name='%s'",
        lname.data);
    nm_db_edit(query.data);

    nm_str_free(&name_copy);
    nm_str_free(&lname);
    nm_str_free(&query);
}

/* vim:set ts=4 sw=4 fdm=marker: */
