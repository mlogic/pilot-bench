/*
 * pilot.cc: main file for the command line pilot tool
 *
 * Copyright (c) 2015, University of California, Santa Cruz, CA, USA.
 * Created by Yan Li <yanli@ucsc.edu, elliot.li.tech@gmail.com>,
 * Department of Computer Science, Baskin School of Engineering.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Storage Systems Research Center, the
 *       University of California, nor the names of its contributors
 *       may be used to endorse or promote products derived from this
 *       software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * REGENTS OF THE UNIVERSITY OF CALIFORNIA BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <algorithm>
#include "benchmark_worker.h"
#include <boost/program_options.hpp>
#include <cassert>
#include <cdk.h>
#include <common.h>
#include "config.h"
#include <iomanip>
#include <iostream>
#include "libpilot.h"
#include <mutex>
#include <string>
#include <vector>

using namespace std;
using namespace pilot;
namespace po = boost::program_options;

#define QUIT            CTRL('Q')
#define ESCAPE          CTRL('[')

const chtype DEFAULT_COLOR = COLOR_PAIR(5);

/**
 * \brief This is a model class for tasks information as in the MVC architecture
 */
class TaskList {
private:
    vector<char*> tasks_;
    vector<bool> enabled_;
public:
    TaskList() {
        tasks_.push_back(strdup("Warm-up removal"));
        tasks_.push_back(strdup("Overhead detection"));
        tasks_.push_back(strdup("Confid. interv. calc"));
        tasks_.push_back(strdup("Percentile calc"));
        tasks_.push_back(strdup("Refining results"));
        tasks_.push_back(strdup("Save results"));
        enabled_.push_back(true);
        enabled_.push_back(false);
        enabled_.push_back(true);
        enabled_.push_back(false);
        enabled_.push_back(false);
        enabled_.push_back(true);
    }
    ~TaskList() {
        for (auto s : tasks_)
            free(s);
    }
    char** data(void) {
        return tasks_.data();
    }
    const vector<bool>& enabled(void) { return enabled_; }
    size_t size(void) {
        return tasks_.size();
    }
};

class BoxedWidget {
private:
    int w_;      //! the width of the box
    int h_;      //! the height of the box
    WINDOW    *wind_;

    void draw(void) {
        wind_ = newwin (h_, w_, y_, x_);
        cdk_screen_ = initCDKScreen(wind_);
        wbkgd(wind_, DEFAULT_COLOR | A_BOLD);
        box (wind_, ACS_VLINE, ACS_HLINE);
        writeChar(cdk_screen_->window,
                  (w_ - title_.size()) / 2,
                  0,
                  const_cast<char*>(title_.c_str()),
                  HORIZONTAL, 0,
                  title_.size());
    }
protected:
    CDKSCREEN *cdk_screen_;
    int x_;      //! the X of the box
    int y_;      //! the Y of the box
    int inner_w_; //! the width of the space for the inner widget
    int inner_h_; //! the height of the space for the inner widget
public:
    const string title_;

    virtual void refresh(void) {
        refreshCDKScreen(cdk_screen_);
    }

    BoxedWidget(int h, int w, int y, int x, const string &title)
    : x_(x), y_(y), w_(w), h_(h), title_(title), wind_(NULL) {
        inner_w_ = w_ - 2;
        inner_h_ = h_ - 2;
        draw();
    }
    virtual ~BoxedWidget() {
        if (cdk_screen_) destroyCDKScreen(cdk_screen_);
        if (wind_) delwin(wind_);
    }
};

class TaskBox {
private:
    int x_;      //! the X of the box
    int y_;      //! the Y of the box
    int w_;      //! the width of the box
    int h_;      //! the height of the box
    CDKSCREEN *cdk_screen_;
    CDKSELECTION *selection_list_;
    CDKLABEL  *title_label_;
    vector<char*> items_;
    TaskList *task_list_;
    const char *title_[1] = {" TASK LIST "};

    const char *choice_list_[2] = {"   ", " X "};

    void draw(void) {
        selection_list_ = newCDKSelection(cdk_screen_,
                                    x_,
                                    y_,
                                    NONE,
                                    h_,
                                    w_,
                                    NULL,
                                    task_list_->data(),
                                    task_list_->size(),
                                    const_cast<char**>(choice_list_),
                                    2,
                                    A_REVERSE,
                                    TRUE,
                                    FALSE);
        for (int i = 0; i < task_list_->enabled().size(); ++i) {
            selection_list_->selections[i] = task_list_->enabled()[i] ? 1 : 0;
        }
        setCDKScrollBackgroundAttrib(selection_list_, DEFAULT_COLOR | A_BOLD);
        title_label_ = newCDKLabel(cdk_screen_, x_ + (w_ - strlen(title_[0])) / 2, y_,
                                   const_cast<char**>(title_), 1, FALSE, FALSE);
        setCDKLabelBackgroundAttrib(title_label_, DEFAULT_COLOR | A_BOLD);
    }
public:
    TaskBox(CDKSCREEN *cdk_screen, int h, int w, int y, int x, TaskList *task_list)
    : cdk_screen_(cdk_screen), x_(x), y_(y), w_(w), h_(h),
      task_list_(task_list) {
        draw();
    }

    ~TaskBox() {
        if (selection_list_) destroyCDKScroll(selection_list_);
        if (title_label_) destroyCDKLabel(title_label_);
    }
};

struct PIInfo {
    std::string pi_name;
    std::string pi_unit;
    pilot_pi_unit_reading_format_func_t *pi_format_func;
};

class SummaryBox : public BoxedWidget {
private:
    const std::vector<PIInfo> * const pi_info_vec_p_;
    static const int kLinesPerPI = 2;

    pilot_workload_info_t *wi_;
    void free_my_workload_info(void) {
        if (!wi_) return;
        free(wi_->total_num_of_unit_readings);
        free(wi_->unit_readings_mean);
        free(wi_->unit_readings_var);
        free(wi_->unit_readings_autocorrelation_coefficient);
        free(wi_->unit_readings_optimal_subsession_size);
        free(wi_->unit_readings_optimal_subsession_var);
        free(wi_->unit_readings_optimal_subsession_autocorrelation_coefficient);
        free(wi_->unit_readings_optimal_subsession_confidence_interval);
        free(wi_->unit_readings_required_sample_size);
        free(wi_);
        wi_ = NULL;
    }
    /**
     * \brief Duplicate wi into wi_ so we can redraw everything later
     * @param wi
     */
    void dup_workload_info(const pilot_workload_info_t *wi) {
        if (!wi_) {
            wi_ = (pilot_workload_info_t*)malloc(sizeof(pilot_workload_info_t));
            die_if(!wi_, ERR_NOMEM, string(__func__) + "() cannot allocate memory");
            memset(wi_, 0, sizeof(*wi_));
        }
        wi_->num_of_rounds = wi->num_of_rounds;

#define COPY_FIELD(field) wi_->field = (typeof(wi->field[0])*)realloc(wi_->field, sizeof(wi->field[0]) * kNumOfPI); \
    memcpy(wi_->field, wi->field, sizeof(wi->field[0]) * kNumOfPI)

        COPY_FIELD(total_num_of_unit_readings);
        COPY_FIELD(unit_readings_mean);
        COPY_FIELD(unit_readings_var);
        COPY_FIELD(unit_readings_autocorrelation_coefficient);
        COPY_FIELD(unit_readings_optimal_subsession_size);
        COPY_FIELD(unit_readings_optimal_subsession_var);
        COPY_FIELD(unit_readings_optimal_subsession_autocorrelation_coefficient);
        COPY_FIELD(unit_readings_optimal_subsession_confidence_interval);
        COPY_FIELD(unit_readings_required_sample_size);

#undef COPY_FIELD
    }

    stringstream draw_buf_;
    int next_draw_pos_x_;
    int next_draw_pos_y_; //! used by draw_data_line to track the location of the next line
    int prev_max_lines_;  //! the maximum number of lines reached so far; next draw needs to do screen clear up to this mark

    void use_default_color(void) {
        wbkgd(cdk_screen_->window, DEFAULT_COLOR | A_BOLD);
    }

    void use_highlight_color(void) {
        wbkgd(cdk_screen_->window, COLOR_PAIR(29) | A_BOLD);
    }
    void flush_buf(void) {
        writeChar(cdk_screen_->window,
                  1 + next_draw_pos_x_,
                  next_draw_pos_y_,
                  const_cast<char*>(draw_buf_.str().c_str()),
                  HORIZONTAL, 0,
                  draw_buf_.str().size());
        next_draw_pos_x_ += draw_buf_.str().size();
        draw_buf_.str(string()); draw_buf_.clear();
    }

    void flush_buf_new_line(void) {
        flush_buf();
        next_draw_pos_x_ = 0;
        ++next_draw_pos_y_;
    }

    template <typename T>
    void draw_data_line(std::string label, T data, std::string tail) {
        // draw label
        use_default_color();
        draw_buf_ << label;
        flush_buf();
        // draw data
        int data_width = inner_w_ - label.size();
        if (tail.size() != 0) data_width -= tail.size();
        use_highlight_color();
        draw_buf_ << setw(data_width) << setprecision(3) << data;
        // does the data fill in one line
        if (draw_buf_.str().size() > inner_w_ - label.size()) {
            ++next_draw_pos_y_;
            next_draw_pos_x_ = 0;
            // clear buf and redraw to make sure we draw all space before the
            // data to clear old characters
            draw_buf_.str(std::string()); draw_buf_.clear();
            draw_buf_ << setw(data_width + label.size()) << data;
        }
        flush_buf();
        //draw tail
        use_default_color();
        draw_buf_ << tail;
        flush_buf_new_line();
    }
    void draw(void) {
        next_draw_pos_y_ = 1;
        if (wi_) {
            draw_data_line("total rounds: ", wi_->num_of_rounds, "");
            for (int piid = 0; piid < kNumOfPI; ++piid) {
                const char pi_separator[] = " Perf. Index ";
                const string &pi_name = (*pi_info_vec_p_)[piid].pi_name;
                const string &pi_unit = std::string(" ") + (*pi_info_vec_p_)[piid].pi_unit;
                pilot_pi_unit_reading_format_func_t *pi_format_func =
                        (*pi_info_vec_p_)[piid].pi_format_func;

                // draw the PI name as the separator
                // decor is the ====== in the PI separator
                int decor_len = (inner_w_ - pi_name.size() - 2/*spaces*/) / 2;
                for (int i = 0; i < decor_len; ++i) draw_buf_ << '=';
                draw_buf_ << " " << pi_name << " ";
                for (int i = decor_len + pi_name.size() + 2; i < inner_w_; ++i) {
                    draw_buf_ << '=';
                }
                use_default_color();
                flush_buf_new_line();

                size_t cur_ur = wi_->total_num_of_unit_readings[piid];
                draw_data_line("sample size: ", cur_ur, "");
                if (0 == wi_->total_num_of_unit_readings[piid]) continue;
                double sm = wi_->unit_readings_mean[piid];
                double var = wi_->unit_readings_var[piid];
                double var_rt = var / sm;
                draw_data_line("sample mean: ", pi_format_func(NULL, sm), pi_unit);
                draw_data_line("variance: ", var_rt * pi_format_func(NULL, sm), pi_unit);

                use_default_color();
                std::string label = "variance to mean ratio: ";
                draw_buf_ << label;
                flush_buf();
                use_highlight_color();
                draw_buf_ << setprecision(3)
                          << setw(inner_w_ - label.size() - 1)
                          << pi_format_func(NULL, var_rt * 100) << "%";
                flush_buf_new_line();

                draw_data_line("autocorrelation coefficient: ",
                               wi_->unit_readings_autocorrelation_coefficient[piid],
                               "");
                size_t q = wi_->unit_readings_optimal_subsession_size[piid];
                draw_data_line("optimal subsession size (q): ", q, "");
                double subvar = wi_->unit_readings_optimal_subsession_var[piid];
                double subvar_rt = subvar / sm;
                draw_data_line("subsession variance: ", subvar_rt * pi_format_func(NULL, sm), "");

                use_default_color();
                label = "subvar. to mean ratio: ";
                draw_buf_ << label;
                flush_buf();
                use_highlight_color();
                draw_buf_ << setw(inner_w_ - label.size() - 1) << subvar_rt * 100 / sm << "%";
                flush_buf_new_line();

                size_t min_ur = wi_->unit_readings_required_sample_size[piid];
                draw_data_line("min. sample size:", min_ur, "");
                draw_data_line("current sample size: ", cur_ur, "");

                use_highlight_color();
                if (cur_ur >= min_ur && cur_ur / q >= 200) {
                    draw_buf_ << "Sample size large enough";
                } else {
                    if (cur_ur < min_ur) {
                        draw_buf_ << "Sample size too small";
                    } else {
                        draw_buf_ << "Sample size MIGHT be too small" << endl;
                    }
                }
                flush_buf_new_line();

                double ci = wi_->unit_readings_optimal_subsession_confidence_interval[piid];
                double ci_rt = ci / sm;
                double ci_low = pi_format_func(NULL, sm) * (1 - ci_rt/2);
                double ci_high = pi_format_func(NULL, sm) * (1 + ci_rt/2) ;
                use_default_color();
                draw_buf_ << "95% confidence interval (CI): ";
                flush_buf_new_line();

                // calculate the len of the CI line
                stringstream buf;
                buf << setprecision(4);
                buf << "[" << ci_low << ", " << ci_high << "]" << pi_unit;
                int ci_width = buf.str().size();

                draw_buf_ << setprecision(4);
                draw_buf_ << setw(inner_w_ - ci_width + 1) << right << "[";
                flush_buf();
                use_highlight_color();
                draw_buf_ << ci_low;
                flush_buf();
                use_default_color();
                draw_buf_ << ", ";
                flush_buf();
                use_highlight_color();
                draw_buf_ << ci_high;
                flush_buf();
                use_default_color();
                draw_buf_ << "]" << pi_unit;
                flush_buf_new_line();

                draw_data_line("CI width: ", ci_high - ci_low, pi_unit);

                use_highlight_color();
                string tail = " of mean";
                draw_buf_ << setw(inner_w_ - tail.size() - 1) << ci_rt * 100 << "%";
                flush_buf();
                use_default_color();
                draw_buf_ << tail;
                flush_buf_new_line();
            } /* loop for all PI */

            // if this refresh cycle has fewer lines than previous cycle,
            // we need to clear the rest of the lines from previous cycle
            int this_max_lines = next_draw_pos_y_;
            while (next_draw_pos_y_ < prev_max_lines_) {
                draw_buf_ << setw(inner_w_) << " ";
                flush_buf_new_line();
            }
            prev_max_lines_ = this_max_lines;

            refresh();
        }
    }
public:

    void show_workload_info(const pilot_workload_info_t *wi) {
        dup_workload_info(wi);
        draw();
    }
    SummaryBox(int h, int w, int y, int x, const std::vector<PIInfo> *pi_info_vec_p)
        : wi_(NULL), pi_info_vec_p_(pi_info_vec_p),
          BoxedWidget(h, w, y, x, " WORKLOAD SUMMARY "),
          next_draw_pos_x_(0), next_draw_pos_y_(0),
          prev_max_lines_(0) {
        draw();
    }
    ~SummaryBox() {
        free_my_workload_info();
    }
};

class MsgBox {
private:
    int x_;      //! the X of the box
    int y_;      //! the Y of the box
    int w_;      //! the width of the box
    int h_;      //! the height of the box
    CDKSCREEN  *cdk_screen_;
    CDKSWINDOW *swindow_;
    CDKLABEL   *title_label_;
    const char *title_[1] = {" MESSAGES "};

    void draw(void) {
        swindow_ = newCDKSwindow(cdk_screen_,
                                 x_,
                                 y_,
                                 h_,
                                 w_,
                                 NULL,
                                 500,
                                 TRUE,
                                 FALSE);
        setCDKSwindowBackgroundAttrib(swindow_, DEFAULT_COLOR | A_BOLD);
        title_label_ = newCDKLabel(cdk_screen_, x_ + (w_ - strlen(title_[0])) / 2, y_,
                                   const_cast<char**>(title_), 1, FALSE, FALSE);
        setCDKLabelBackgroundAttrib(title_label_, DEFAULT_COLOR | A_BOLD);
    }
public:
    MsgBox(CDKSCREEN *cdk_screen, int h, int w, int y, int x)
    : cdk_screen_(cdk_screen), x_(x), y_(y), w_(w), h_(h - 1) {
        draw();
    }
    ~MsgBox() {
        if (swindow_) destroyCDKSwindow(swindow_);
    }
    void add_msg(const string &msg) {
        assert (swindow_);
        addCDKSwindow (swindow_, msg.c_str(), BOTTOM);
    }
};

/**
 * \brief This is the view class as in the MVC architecture
 * \details This class is thread-safe
 */
class PilotTUI {
private:
    mutex      lock_;

    TaskList   *task_list_;
    std::vector<PIInfo> pi_info_vec_;
    const int   num_of_pi_;

    WINDOW     *curses_win_;
    CDKSCREEN  *cdk_screen_;

    CDKLABEL   *title_bar_;
    CDKLABEL   *progress_bar_;
    CDKLABEL   *bottom_bar_a_;
    CDKLABEL   *bottom_bar_b_;
    TaskBox    *task_box_;
    SummaryBox *summary_box_;
    MsgBox     *msg_box_;
    int         left_col_width_;
    const int  task_box_height_;
    const char *_title[1] = {"</57> PILOT v0.2 <#VL> "};
    const char *bottom_txt_[1] = {"</57> <!57></9>F1<!9></57> Help  <!57></9>Q<!9></57> Quit<!57>"};
    int         bottom_txt_len_ = 16;

    stringstream msg_ss_buf_; // used by the << operator

    // this function must not be called in parallel
    void _flush_msg_ss_buf_nonlock(void) {
        if (msg_ss_buf_.str().size() == 0)
            return;
        // here we use a prefix to add some color if the output has no color yet
        if (msg_ss_buf_.str()[0] != '<' ) {
            static const char prefix[] = "</21>";
            char *buf = new char[sizeof(prefix) + msg_ss_buf_.str().size()];
            strcpy(buf, prefix);
            strcpy(buf + sizeof(prefix) - 1, msg_ss_buf_.str().c_str());
            msg_box_->add_msg(buf);
            delete[] buf;
        } else {
            msg_box_->add_msg(msg_ss_buf_.str().c_str());
        }
        // clear msg_ss_buf_
        msg_ss_buf_.str(std::string());
        msg_ss_buf_.clear();
    }
public:
    PilotTUI(TaskList *task_list, const std::vector<PIInfo> &pi_info_vec) : task_list_(task_list),
        num_of_pi_(pi_info_vec.size()), pi_info_vec_(pi_info_vec),
        task_box_height_(task_list->size() + 2),
        title_bar_(NULL), progress_bar_(NULL), task_box_(NULL),
        summary_box_(NULL), msg_box_(NULL) {
        // the width of the left column is [35, 50]
        left_col_width_ = max(COLS / 3, 35);
        left_col_width_ = min(50, left_col_width_);

        curses_win_ = initscr();
        // Hide the cursor
        curs_set(0);
        cdk_screen_ = initCDKScreen(curses_win_);
        initCDKColor();
        draw();
        refresh();
    }
    ~PilotTUI() {
        delete msg_box_;
        delete summary_box_;
        delete task_box_;
        destroyCDKLabel(bottom_bar_b_);
        destroyCDKLabel(bottom_bar_a_);
        destroyCDKLabel(title_bar_);
        destroyCDKScreen(cdk_screen_);
        endCDK();
    }
    void draw() {
        lock_guard<mutex> lock(lock_);

        if (title_bar_) {
            // this is a redraw
            destroyCDKLabel(title_bar_);
            destroyCDKLabel(bottom_bar_b_);
            destroyCDKLabel(bottom_bar_a_);
            delete msg_box_;
            delete summary_box_;
            delete task_box_;
        }
        title_bar_ = newCDKLabel(cdk_screen_, 0, 0,
                                 const_cast<char**>(_title), 1, FALSE, FALSE);
        // draw the first half the bottom bar
        bottom_bar_a_ = newCDKLabel(cdk_screen_, 0, LINES - 1,
                                    const_cast<char**>(bottom_txt_), 1, FALSE, FALSE);
        // draw the second half the bottom bar, which is empty that fills the rest of the line
        char *buf[1];
        int second_half_len = COLS - bottom_txt_len_ + 1;
        buf[0] = new char[second_half_len];
        memset(buf[0], ' ', second_half_len - 1);
        buf[0][second_half_len - 1] = 0;
        bottom_bar_b_ = newCDKLabel(cdk_screen_, bottom_txt_len_, LINES - 1, buf, 1, FALSE, FALSE);
        setCDKLabelBackgroundAttrib(bottom_bar_b_, COLOR_PAIR(57));
        delete[] buf[0];

        task_box_ = new TaskBox(cdk_screen_,
                                task_box_height_,
                                left_col_width_,
                                1,
                                0,
                                task_list_);
        summary_box_ = new SummaryBox(LINES - task_box_height_ - 2,
                                      left_col_width_,
                                      task_box_height_ + 1,
                                      0, &pi_info_vec_);
        msg_box_ = new MsgBox(cdk_screen_, LINES - 2, COLS - left_col_width_, 1, left_col_width_);
    }
    void refresh() {
        lock_guard<mutex> lock(lock_);
        refreshCDKScreen(cdk_screen_);
        summary_box_->refresh();
    }

    void event_loop() {
        int ch = ERR;
        do {
            switch (ch) {
            default:
                beep();
                break;
            case ERR:
                break;
            }
            // no need to refresh every time
        } while ((ch = getch()) != ERR && ch != QUIT && ch != ESCAPE && ch != 'Q' && ch != 'q');
        printf ("%d\n", ch);
    }
    void flush(void) {
        lock_guard<mutex> lock(lock_);
        _flush_msg_ss_buf_nonlock();
    }
    /**
     * \brief Flush the message stringstream buffer
     */
    template <typename R>
    PilotTUI& operator<<(const R &t) {
        lock_guard<mutex> lock(lock_);
        msg_ss_buf_ << t;
        return *this;
    }
    PilotTUI& operator<<(const stringstream &s) {
        *this << s.str().c_str();
        return *this;
    }
    PilotTUI& operator<<(char *t) {
        return (*this) << const_cast<const char*>(t);
    }
    PilotTUI& operator<<(const char *t) {
        lock_guard<mutex> lock(lock_);
        while (*t != 0) {
            if ('\n' == *t || '\r' == *t) {
                msg_ss_buf_ << std::endl;
                _flush_msg_ss_buf_nonlock();
            } else {
                msg_ss_buf_ << *t;
            }
            ++t;
        }
        return *this;
    }
    // for endl
    PilotTUI& operator<<(std::ostream& (*pf)(std::ostream&)) {
        lock_guard<mutex> lock(lock_);
        msg_ss_buf_ << std::endl;
        _flush_msg_ss_buf_nonlock();
        return *this;
    }
    PilotTUI& operator<<(const pilot_workload_info_t *wi) {
        lock_guard<mutex> lock(lock_);
        assert (summary_box_);
        summary_box_->show_workload_info(wi);
        return *this;
    }
    PilotTUI& operator<<(pilot_workload_info_t *wi) {
        return (*this) << const_cast<const pilot_workload_info_t*>(wi);
    }
};

int main(int argc, char** argv) {
    PILOT_LIB_SELF_CHECK;
    stringstream ss;
    // Parsing the command line arguments
    po::options_description desc("Generates a non-zero sequential write I/O workload and demonstrates how to use libpilot");
    // Don't use po::value<>()->required() here or --help wouldn't work
    desc.add_options()
            ("help", "produce help message")
            ("fsync,f", "call fsync() after each I/O request")
            ("io-size,s", po::value<size_t>(), "the size of I/O operations (default to 1 MB)")
            ("length-limit,l", po::value<size_t>(), "the max. length of the workload in bytes (default to 800*1024*1024); "
                    "the workload will not write beyond this limit")
            ("init-length,i", po::value<size_t>(), "the initial length of workload in bytes (default to 1/10 of limitsize); "
                    "the workload will start from this length and be gradually repeated or increased until the desired "
                    "confidence level is reached")
            ("output,o", po::value<string>(), "set output file name, can be a device. WARNING: the file will be overwritten if it exists.")
            ("result,r", po::value<string>(), "set result file name, (default to seq-write.csv)")
            ("warm-up-io,w", po::value<size_t>(), "the number of I/O operations that will be removed from the beginning as the warm-up phase (default to 1/10 of total IO ops)")
            ("verbose,v", "print more debug information")
            ;

    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);
    } catch (po::error &e) {
        cout << e.what() << endl;
        return 1;
    }

    if (vm.count("help")) {
        cout << desc << endl;
        return 2;
    }

    // verbose
    pilot_set_log_level(lv_warning);
    bool verbose = false;
    if (vm.count("verbose")) {
        pilot_set_log_level(lv_trace);
        verbose = true;
    }

    // fsync
    bool fsync = false;
    if (vm.count("fsync")) {
        fsync = true;
    }

    string output_file;
    if (vm.count("output")) {
        output_file = vm["output"].as<string>();
        ss << "Output file is set to " << output_file << endl;
    } else {
        cout << "Error: output file was not set." << endl;
        cout << desc << endl;
        return 2;
    }

    size_t io_size = 1*1024*1024;
    if (vm.count("io-size")) {
        if (vm["io-size"].as<size_t>() <= 0) {
            cout << "I/O size must be larger than 0" << endl;
            return 1;
        }
        io_size = vm["io-size"].as<size_t>();
        ss << "I/O size is set to ";
    } else {
        ss << "Using default IO size: 1";
    }
    if (io_size >= MEGABYTE) {
        ss << io_size / MEGABYTE << " MB" << endl;
    } else {
        ss << io_size << " bytes" << endl;
    }

    string result_file("seq-write.csv");
    if (vm.count("result")) {
        result_file = vm["result"].as<string>();
    }

    size_t io_limit = 800*1024*1024;
    if (vm.count("length-limit")) {
        if (vm["length-limit"].as<size_t>() <= 0) {
            cout << "I/O limit must be larger than 0" << endl;
            return 1;
        }
        io_limit = vm["length-limit"].as<size_t>();
    }
    ss << "I/O limit is set to ";
    if (io_limit >= MEGABYTE) {
        ss << io_limit / MEGABYTE << " MB"<< endl;
    } else {
        ss << io_limit << " bytes" << endl;
    }

    size_t init_length;
    if (vm.count("init-length"))
        init_length = vm["init-length"].as<size_t>();
    else
        init_length = io_limit / 5;

    TaskList task_list;
    PilotTUI pilot_tui(&task_list, std::vector<PIInfo>{{"Write throughput", "MB/s", &ur_format_func}});
    pilot_tui << ss;
    BenchmarkWorker<PilotTUI> benchmark_worker(io_size, io_limit, init_length,
                                               output_file, result_file, fsync,
                                               pilot_tui);
    benchmark_worker.start();
    pilot_tui.event_loop();
    benchmark_worker.join();

    //const double* time_readings = pilot_get_pi_unit_readings(wl, time_pi, 0, &num_of_work_units) + warmupio;
    //num_of_work_units -= warmupio;

    return 0;
}
